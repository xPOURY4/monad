#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/instruction.h>
#include <compiler/ir/x86/virtual_stack.h>
#include <compiler/types.h>

#include <utils/assert.h>

#include <evmc/evmc.hpp>

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

namespace monad::compiler::stack
{
    Stack::Stack()
        : top_index_{-1}
        , deferred_comparison_index_{std::nullopt}
        , min_delta_{0}
        , max_delta_{0}
        , delta_{0}
        , raw_stack_argument_byte_size_{0}
    {
        for (uint8_t i = 0; i < 31; ++i) {
            free_avx_regs_.emplace(i);
        }
    }

    Stack::Stack(basic_blocks::Block const &block)
        : Stack()
    {
        include_block(block);
    }

    void Stack::include_block(basic_blocks::Block const &block)
    {
        auto prev_min_delta = min_delta_;
        auto prev_max_delta = max_delta_;

        // The maximum number of arguments that could be passed to a runtime
        // library function in this block. The analysis that computes this is
        // imprecise in that we overestimate the size of required argument
        // space, but this is sound because an alternative valid implementation
        // would be to use a large constant value instead.
        auto max_call_args = std::size_t{0};

        for (auto const &instr : block.instrs) {
            delta_ -= instr.info().min_stack;
            min_delta_ = std::min(delta_, min_delta_);

            // We need to treat SWAP and DUP slightly differently to other
            // instructions; they require that the minimum delta is adjusted to
            // ensure a big enough input stack, but because they don't actually
            // consume these elements, this change shouldn't be reflected in the
            // net delta.
            if (instr.code == basic_blocks::InstructionCode::Swap ||
                instr.code == basic_blocks::InstructionCode::Dup) {
                delta_ += instr.info().min_stack;
            }

            delta_ += instr.info().increases_stack;
            max_delta_ = std::max(delta_, max_delta_);

            // Instructions can take an additional two arguments when they call
            // a runtime function (sret pointer, context pointer). Rather than
            // listing precisely which ones use which arguments, just assume
            // that all of them use both.
            max_call_args = std::max(max_call_args, instr.info().num_args + 2);
        }

        if (max_call_args > 6) {
            raw_stack_argument_byte_size_ = (max_call_args - 6) * 8;
        }

        delta_ -= basic_blocks::terminator_inputs(block.terminator);
        min_delta_ = std::min(delta_, min_delta_);

        for (auto i = prev_min_delta - 1; i >= min_delta_; --i) {
            negative_elements_.emplace_back(StackOffset(i));
        }

        for (auto i = prev_max_delta; i < max_delta_; ++i) {
            auto [_, inserted] = available_stack_indices_.insert(i);
            MONAD_COMPILER_ASSERT(inserted);
        }
    }

    std::size_t Stack::stack_argument_byte_size() const
    {
        return (raw_stack_argument_byte_size_ + 31) & ~std::size_t{31};
    }

    std::int64_t Stack::index_to_sp_offset(std::int64_t index) const
    {
        auto base_offset = -(index + 1 - max_delta_) * 32;
        return base_offset +
               static_cast<std::int64_t>(stack_argument_byte_size());
    }

    StackElement const &Stack::index(std::int64_t index) const
    {
        if (index < 0) {
            auto i = static_cast<std::size_t>(-index - 1);
            MONAD_COMPILER_ASSERT(i < negative_elements_.size());
            return negative_elements_[i];
        }
        else {
            auto i = static_cast<std::size_t>(index);
            MONAD_COMPILER_ASSERT(i < positive_elements_.size());
            return positive_elements_[i];
        }
    }

    StackElement &Stack::index(std::int64_t index)
    {
        if (index < 0) {
            auto i = static_cast<std::size_t>(-index - 1);
            MONAD_COMPILER_ASSERT(i < negative_elements_.size());
            return negative_elements_[i];
        }
        else {
            auto i = static_cast<std::size_t>(index);
            MONAD_COMPILER_ASSERT(i < positive_elements_.size());
            return positive_elements_[i];
        }
    }

    std::pair<Operand, bool>
    Stack::index_operand(std::int64_t stack_index) const
    {
        return std::visit(
            Cases{
                [](Operand op) { return std::pair{op, false}; },

                [this](Duplicate dup) {
                    auto [original, refcount] = dups_[dup.offset];
                    return std::pair{original, refcount == 1};
                },

                [](DeferredComparison) -> std::pair<Operand, bool> {
                    std::unreachable();
                },
            },
            index(stack_index));
    }

    StackElement Stack::top() const
    {
        return index(top_index_);
    }

    void Stack::pop_operand(Operand op)
    {
        std::visit(
            Cases{
                [this](AvxRegister a) {
                    auto removed = avx_reg_stack_indices_.erase(top_index_);
                    MONAD_COMPILER_ASSERT(removed == 1);
                    free_avx_regs_.push(a);
                },

                [this](StackOffset s) {
                    auto [_, inserted] =
                        available_stack_indices_.insert(s.offset);
                    MONAD_COMPILER_ASSERT(inserted);
                },

                [](auto) {},
            },
            op);
    }

    void Stack::pop()
    {
        std::visit(
            Cases{
                [this](Operand op) { pop_operand(op); },

                [this](Duplicate d) {
                    auto &dup = dups_[d.offset];

                    dup.second -= 1;
                    if (dup.second == 0) {
                        pop_operand(dup.first);
                    }
                },

                [this](DeferredComparison) {
                    MONAD_COMPILER_ASSERT(
                        deferred_comparison_index_.has_value());
                    deferred_comparison_index_ = std::nullopt;
                },
            },
            top());

        // Note that it's valid for stack indices to become negative here.
        top_index_ -= 1;
    }

    void Stack::pop(std::size_t n)
    {
        for (auto i = 0u; i < n; ++i) {
            pop();
        }
    }

    void Stack::push(StackElement elem)
    {
        top_index_ += 1;

        auto push_operand = [this](Operand op) {
            std::visit(
                Cases{
                    [this](AvxRegister) {
                        auto [_, inserted] =
                            avx_reg_stack_indices_.insert(top_index_);
                        MONAD_COMPILER_ASSERT(inserted);
                    },

                    [this](StackOffset s) {
                        auto removed = available_stack_indices_.erase(s.offset);
                        MONAD_COMPILER_ASSERT(removed == 1);
                    },

                    [](auto) {},
                },
                op);
        };

        std::visit(
            Cases{
                [this](DeferredComparison) {
                    MONAD_COMPILER_ASSERT(
                        !deferred_comparison_index_.has_value());
                    deferred_comparison_index_ = top_index_;
                },

                push_operand,

                [](auto) {},
            },
            elem);

        if (top_index_ ==
            static_cast<std::int64_t>(positive_elements_.size())) {
            positive_elements_.push_back(elem);
        }
        else {
            index(top_index_) = elem;
        }
    }

    void Stack::dup(std::int64_t stack_index)
    {
        auto dup_index = std::visit(
            Cases{
                [this](Duplicate d) {
                    dups_[d.offset].second += 1;
                    return d.offset;
                },

                [this, stack_index](Operand op) {
                    auto i = dups_.size();
                    dups_.emplace_back(op, 2);
                    index(stack_index) = Duplicate(i);
                    return i;
                },

                [](DeferredComparison) -> std::size_t {
                    // We do not allow dup of deferred comparison, although it
                    // is possible to add support for this.
                    std::unreachable();
                },
            },
            index(stack_index));

        push(Duplicate(dup_index));
    }

    void Stack::swap(std::int64_t swap_index)
    {
        auto top_elem = top();
        auto swapped_elem = index(swap_index);

        if (std::holds_alternative<DeferredComparison>(top_elem)) {
            MONAD_COMPILER_ASSERT(
                deferred_comparison_index_.has_value() &&
                *deferred_comparison_index_ == top_index_);

            deferred_comparison_index_ = swap_index;
        }

        if (std::holds_alternative<DeferredComparison>(swapped_elem)) {
            MONAD_COMPILER_ASSERT(
                deferred_comparison_index_.has_value() &&
                *deferred_comparison_index_ == swap_index);

            deferred_comparison_index_ = top_index_;
        }

        index(top_index_) = swapped_elem;
        index(swap_index) = top_elem;

        auto has_top_avx_index =
            (avx_reg_stack_indices_.erase(top_index_) == 1);

        auto has_swapped_avx_index =
            (avx_reg_stack_indices_.erase(swap_index) == 1);

        if (has_top_avx_index) {
            avx_reg_stack_indices_.insert(swap_index);
        }

        if (has_swapped_avx_index) {
            avx_reg_stack_indices_.insert(top_index_);
        }
    }

    std::int64_t Stack::find_spill_offset(std::int64_t stack_index) const
    {
        if (available_stack_indices_.contains(stack_index)) {
            return stack_index;
        }

        if (!available_stack_indices_.empty()) {
            return *available_stack_indices_.begin();
        }

        MONAD_COMPILER_ASSERT(false);
    }

    std::int64_t Stack::get_available_stack_offset(std::int64_t stack_index)
    {
        auto spill_offset = find_spill_offset(stack_index);
        auto removed = available_stack_indices_.erase(spill_offset);

        if (removed != 1) {
            MONAD_COMPILER_ASSERT(
                false && "Found a stack offset not available for reuse");
        }

        return spill_offset;
    }

    std::optional<std::pair<Operand, std::int64_t>>
    Stack::get_existing_stack_slot(StackElement original)
    {
        // If the original stack element is a stack offset or a duplicate of
        // one, it is already in memory and we can just return the original
        // operand directly.

        if (auto *op = std::get_if<Operand>(&original)) {
            if (auto *s_off = std::get_if<StackOffset>(op)) {
                return std::pair{*s_off, s_off->offset};
            }
        }

        if (auto *dup = std::get_if<Duplicate>(&original)) {
            if (auto *s_off =
                    std::get_if<StackOffset>(&dups_[dup->offset].first)) {
                return std::pair{*s_off, s_off->offset};
            }
        }

        return std::nullopt;
    }

    std::pair<Operand, std::int64_t>
    Stack::spill_stack_index(std::int64_t stack_index)
    {
        auto original = index(stack_index);

        if (auto existing = get_existing_stack_slot(original)) {
            return *existing;
        }

        // Identify a viable spill index and mark it as unavailable.
        auto spill_offset = get_available_stack_offset(stack_index);

        // If there was an operand on the stack already, we overwrite
        // it with the new stack slot and return the original value to
        // be handled by the caller.
        auto operand = std::visit(
            Cases{
                [this, stack_index, spill_offset](Operand op) {
                    index(stack_index) = StackOffset(spill_offset);
                    return op;
                },

                [this, spill_offset](Duplicate d) {
                    auto op = dups_[d.offset].first;
                    dups_[d.offset].first = StackOffset(spill_offset);
                    return op;
                },

                [](DeferredComparison) -> Operand { std::unreachable(); },
            },
            original);

        // If the operand we spilled was an AVX register, we can now
        // mark that register as available for reuse.
        std::visit(
            Cases{
                [this, stack_index](AvxRegister a) {
                    free_avx_regs_.push(a);
                    auto removed = avx_reg_stack_indices_.erase(stack_index);
                    MONAD_COMPILER_ASSERT(removed == 1);
                },

                [](auto) {},
            },
            operand);

        return {operand, spill_offset};
    }

    std::pair<AvxRegister, std::int64_t> Stack::spill_avx_reg()
    {
        MONAD_COMPILER_ASSERT(!avx_reg_stack_indices_.empty());
        auto stack_index = *avx_reg_stack_indices_.begin();

        auto [op, offset] = spill_stack_index(stack_index);

        MONAD_COMPILER_ASSERT(std::holds_alternative<AvxRegister>(op));
        return {std::get<AvxRegister>(op), offset};
    }

    std::pair<Operand, std::int64_t> Stack::push_spilled_output_slot()
    {
        push(Literal(evmc::bytes32(0)));
        return spill_stack_index(top_index_);
    }

    std::int64_t Stack::min_delta() const
    {
        return min_delta_;
    }

    std::int64_t Stack::max_delta() const
    {
        return max_delta_;
    }

    std::int64_t Stack::delta() const
    {
        return delta_;
    }

    std::size_t Stack::size() const
    {
        auto ret = (top_index_ + 1) - min_delta_;
        MONAD_COMPILER_ASSERT(ret >= 0);
        return static_cast<std::size_t>(ret);
    }

    bool Stack::empty() const
    {
        return size() == 0;
    }

    bool operator==(Literal const &a, Literal const &b)
    {
        return a.value == b.value;
    }

    bool operator==(StackOffset const &a, StackOffset const &b)
    {
        return a.offset == b.offset;
    }

    bool operator==(Duplicate const &a, Duplicate const &b)
    {
        return a.offset == b.offset;
    }

    bool operator==(DeferredComparison const &a, DeferredComparison const &b)
    {
        return a.comparison == b.comparison;
    }

    std::strong_ordering operator<=>(AvxRegister const &a, AvxRegister const &b)
    {
        return a.reg <=> b.reg;
    }

    bool operator==(AvxRegister const &a, AvxRegister const &b)
    {
        return a.reg == b.reg;
    }
}
