#pragma once

#include <compiler/ir/local_stacks.h>

#include <evmc/evmc.hpp>

#include <compare>
#include <cstdint>
#include <optional>
#include <queue>
#include <set>
#include <variant>
#include <vector>

namespace monad::compiler::stack
{
    struct Literal;
    struct StackOffset;
    struct AvxRegister;

    using AvxRegisterQueue = std::priority_queue<
        AvxRegister, std::vector<AvxRegister>, std::greater<AvxRegister>>;

    using Operand = std::variant<Literal, StackOffset, AvxRegister>;

    struct Duplicate;
    struct DeferredComparison;

    using StackElement = std::variant<Operand, Duplicate, DeferredComparison>;

    /**
     * A `Stack` manages a virtual representation of the EVM stack, specialized
     * for the resources available on AVX2 x86 machines.
     *
     * This virtual representation can be interpreted by a code-generating
     * component to emit x86 code for an EVM basic block; no code is generated
     * by the stack itself when operations are performed. Instead, the wrapping
     * code is responsible for emitting code that _performs_ the concrete
     * operations corresponding to the virtual ones.
     *
     * Three types of operand can appear on the virtual stack, each of which
     * represents a 256-bit EVM word being stored in a different concrete
     * location:
     * - Literal word values
     * - 256-bit (%ymmN) AVX registers
     * - Offsets into stack memory (that when added to a stack pointer, produce
     *   a pointer that points to a 32-byte EVM word).
     *
     * Additionally, the stack maintains additional book-keeping data for
     * optimisation:
     * - An internal table of reference-counted duplications such that only a
     *   single concrete value must be stored, no matter how many times it is
     *   duplicated.
     * - When the result of a comparison is placed onto the stack, we defer
     *   materialising it to an EVM word from the x86 eflags. If the result is
     *   immediately used for control flow, we can directly refer to the flags
     *   set by the comparison.
     *
     * Stack manipulations over this representation (push, pop, swap, dup) take
     * care of managing resources (free registers, memory locations)
     * appropriately.
     */
    class Stack
    {
    public:
        /**
         * Initialise a stack with deltas computed from the given block.
         *
         * No actual stack manipulations are performed in the constructor; this
         * is because the calling code must inspect each instruction to perform
         * code generation while also updating the stack.
         */
        Stack(local_stacks::Block const &);

        /**
         * The number of bytes required to hold function arguments passed on
         * the X86 stack.
         *
         * The value returned will be 32-byte aligned.
         */
        std::size_t stack_argument_byte_size() const;

        /**
         * Map a logical stack index onto a byte offset relative to `%rsp`.
         */
        std::int64_t index_to_sp_offset(std::int64_t index) const;

        /**
         * Obtain a reference to an item on the stack, correctly
         * handling negative values to reference input stack elements.
         */
        StackElement const &index(std::int64_t index) const;

        /**
         * Obtain a mutable reference to an item on the stack, correctly
         * handling negative values to reference input stack elements.
         */
        StackElement &index(std::int64_t index);

        /**
         * Obtain a copy of the `Operand` at the supplied stack index, along
         * with a flag indicating whether this operand is the sole remaining
         * duplicate of another operand on the stack.
         *
         * Unsafe to call if the stack element at this index may be a deferred
         * comparison.
         */
        std::pair<Operand, bool> index_operand(std::int64_t index) const;

        /**
         * Obtain a copy of the top item of the stack.
         */
        StackElement top() const;

        /**
         * Pop the top element from the stack.
         *
         * This method handles discharging reference counts to duplicated stack
         * elements, and keeping track of deferred comparisons. It defers
         * management of stack resources (memory, AVX registers) to
         * `pop_operand`.
         */
        void pop();

        /**
         * Convenience overload to avoid having to write loops or repetitive
         * code when calling `pop`.
         */
        void pop(std::size_t n);

        /**
         * Push a new operand onto the top of the stack, updating book-keeping
         * information.
         */
        void push(StackElement elem);

        /**
         * Push a duplicate of the specified stack element to the top of the
         * stack.
         *
         * Once an operand is duplicated, all references to it are managed
         * through the table of reference-counted duplicates (i.e. the original
         * operand is moved into the table, and its previous stack slot is made
         * into a duplicate).
         */
        void dup(std::int64_t stack_index);

        /**
         * Swap the top element of the stack with the one at the specified
         * index.
         */
        void swap(std::int64_t swap_index);

        /**
         * Find an available physical stack index that can be used to spill the
         * virtual stack item at this index, and mark that physical index as
         * unavailable.
         */
        std::int64_t get_available_stack_offset(std::int64_t stack_index);

        /**
         * Spill whatever is currently on the stack at the specified index into
         * memory, returning the original operand on the stack, and the
         * corresponding stack offset it has been spilled to.
         *
         * The caller of this function is responsible for interpreting the
         * return value to actually perform the spill; for a returned pair `(op,
         * idx)` the caller should emit code to move `op` onto the stack
         * location `idx`.
         */
        std::pair<Operand, std::int64_t>
        spill_stack_index(std::int64_t stack_index);

        /**
         * Spill the first stack index containing an AVX register to memory.
         *
         * This can be used to free up an AVX register to hold a temporary
         * value, or to ensure that all AVX registers are preserved when making
         * a function call.

         * Unsafe to call if the stack does not contain any AVX register items.
         */
        std::pair<AvxRegister, std::int64_t> spill_avx_reg();

        /**
         * Push a fresh X86 stack slot that is known to be unused to the virtual
         * stack.
         *
         * This function is intended to provide a stack slot that can be safely
         * used as the output from an EVM arithmetic operation or runtime
         * function. The provided slot is not initialized.
         */
        std::pair<Operand, std::int64_t> push_spilled_output_slot();

        /**
         * The relative size of the stack at the *lowest* point during execution
         * of a block.
         */
        std::int64_t min_delta() const;

        /**
         * The relative size of the stack at the *highest* point during
         * execution of a block.
         */
        std::int64_t max_delta() const;

        /**
         * The difference between the final and initial stack sizes during
         * execution of a block.
         */
        std::int64_t delta() const;

        /**
         * The number of elements currently pushed to the stack.
         */
        std::size_t size() const;

        /**
         * Returns false if there are any elements pushed to the stack.
         */
        bool empty() const;

    private:
        /**
         * Initialize an empty stack.
         *
         * This method is private as an empty stack is not useful to consume
         * from an external perspective; a basic block needs to have been
         * ingested for the offsets and computed values in the stack to be
         * useful.
         */
        Stack();

        /**
         * Updates the stack's minimum and maximum deltas to reflect the effect
         of this block's instructions.
         */
        void include_block(local_stacks::Block const &block);

        /**
         * Internal helper method to perform resource management when popping an
         * operand from the stack.
         *
         * This method is responsible for maintaining the sets of free stack
         * offsets and AVX registers, but callers must ensure they handle stack
         * duplications before calling this method.
         */
        void pop_operand(Operand op);

        /**
         * Identify a stack index that can be used to spill the specified stack
         * item.
         *
         * If there's nothing currently in the physical stack slot corresponding
         * to that item, it can be spilled to its "proper" location. Otherwise,
         * if there's a collision, we need to use another available slot to
         * relocate this item to.
         */
        std::int64_t find_spill_offset(std::int64_t stack_index) const;

        /**
         * Helper method for `find_spill_offset` that handles the cases where an
         * element is already located in stack memory.
         */
        std::optional<std::pair<Operand, std::int64_t>>
        get_existing_stack_slot(StackElement op);

        std::vector<StackElement> negative_elements_;
        std::vector<StackElement> positive_elements_;
        std::int64_t top_index_;
        std::vector<std::pair<Operand, std::uint64_t>> dups_;
        std::set<std::int64_t> available_stack_indices_;
        AvxRegisterQueue free_avx_regs_;
        std::set<std::int64_t> avx_reg_stack_indices_;
        std::optional<std::int64_t> deferred_comparison_index_;
        std::int64_t min_delta_;
        std::int64_t max_delta_;
        std::int64_t delta_;
        std::size_t raw_stack_argument_byte_size_;
    };

    struct Literal
    {
        evmc::bytes32 value;
    };

    bool operator==(Literal const &a, Literal const &b);

    struct StackOffset
    {
        std::int64_t offset;
    };

    bool operator==(StackOffset const &a, StackOffset const &b);

    struct AvxRegister
    {
        uint8_t reg;
    };

    std::strong_ordering
    operator<=>(AvxRegister const &a, AvxRegister const &b);

    bool operator==(AvxRegister const &a, AvxRegister const &b);

    struct Duplicate
    {
        std::size_t offset;
    };

    bool operator==(Duplicate const &a, Duplicate const &b);

    enum class Comparison
    {
        Below,
        AboveEqual,
        Above,
        BelowEqual,
        Less,
        GreaterEqual,
        Greater,
        LessEqual,
        Equal,
        NotEqual,
    };

    constexpr Comparison negate_comparison(Comparison c)
    {
        using enum Comparison;
        switch (c) {
        case Below:
            return AboveEqual;
        case AboveEqual:
            return Below;
        case Above:
            return BelowEqual;
        case BelowEqual:
            return Above;
        case Less:
            return GreaterEqual;
        case GreaterEqual:
            return Less;
        case Greater:
            return LessEqual;
        case LessEqual:
            return Greater;
        case Equal:
            return NotEqual;
        case NotEqual:
            return Equal;
        }
    }

    struct DeferredComparison
    {
        Comparison comparison;
    };

    bool operator==(DeferredComparison const &a, DeferredComparison const &b);
}
