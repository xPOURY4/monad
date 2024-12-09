#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/instruction.h>
#include <compiler/ir/x86/virtual_stack.h>
#include <compiler/types.h>

#include <memory>
#include <set>
#include <tuple>
#include <utils/assert.h>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace monad::compiler::native
{
    bool operator==(Literal const &a, Literal const &b)
    {
        return a.value == b.value;
    }

    bool operator==(StackOffset const &a, StackOffset const &b)
    {
        return a.offset == b.offset;
    }

    std::strong_ordering operator<=>(AvxReg const &a, AvxReg const &b)
    {
        return a.reg <=> b.reg;
    }

    bool operator==(AvxReg const &a, AvxReg const &b)
    {
        return a.reg == b.reg;
    }

    std::strong_ordering operator<=>(GeneralReg const &a, GeneralReg const &b)
    {
        return a.reg <=> b.reg;
    }

    bool operator==(GeneralReg const &a, GeneralReg const &b)
    {
        return a.reg == b.reg;
    }

    StackElem::StackElem(Stack *s)
        : stack_{*s}
        , reserve_avx_reg_count_{}
        , reserve_general_reg_count_{}
    {
    }

    std::int32_t StackElem::preferred_stack_offset() const
    {
        return stack_indices_.begin() == stack_indices_.end()
                   ? stack_.min_delta_
                   : *stack_indices_.begin();
    }

    void StackElem::deferred_comparison(Comparison c)
    {
        MONAD_COMPILER_ASSERT(
            stack_.deferred_comparison_.stack_elem == nullptr);
        MONAD_COMPILER_DEBUG_ASSERT(
            !stack_offset_ && !avx_reg_ && !general_reg_ && !literal_);
        stack_.deferred_comparison_.stack_elem = this;
        stack_.deferred_comparison_.comparison = c;
    }

    void StackElem::deferred_comparison()
    {
        MONAD_COMPILER_ASSERT(!stack_.deferred_comparison_.stack_elem);
        MONAD_COMPILER_ASSERT(stack_.deferred_comparison_.negated_stack_elem);
        MONAD_COMPILER_DEBUG_ASSERT(
            stack_.deferred_comparison_.negated_stack_elem != this);
        MONAD_COMPILER_DEBUG_ASSERT(
            !stack_offset_ && !avx_reg_ && !general_reg_ && !literal_);
        stack_.deferred_comparison_.stack_elem = this;
    }

    void StackElem::negated_deferred_comparison()
    {
        MONAD_COMPILER_ASSERT(!stack_.deferred_comparison_.negated_stack_elem);
        MONAD_COMPILER_ASSERT(stack_.deferred_comparison_.stack_elem);
        MONAD_COMPILER_DEBUG_ASSERT(
            stack_.deferred_comparison_.stack_elem != this);
        MONAD_COMPILER_DEBUG_ASSERT(
            !stack_offset_ && !avx_reg_ && !general_reg_ && !literal_);
        stack_.deferred_comparison_.negated_stack_elem = this;
    }

    void StackElem::discharge_deferred_comparison()
    {
        MONAD_COMPILER_DEBUG_ASSERT(
            !stack_offset_ && !avx_reg_ && !general_reg_ && !literal_);
        MONAD_COMPILER_DEBUG_ASSERT(
            stack_.deferred_comparison_.stack_elem == this);
        stack_.deferred_comparison_.stack_elem = nullptr;
    }

    void StackElem::discharge_negated_deferred_comparison()
    {
        MONAD_COMPILER_DEBUG_ASSERT(
            !stack_offset_ && !avx_reg_ && !general_reg_ && !literal_);
        MONAD_COMPILER_DEBUG_ASSERT(
            stack_.deferred_comparison_.negated_stack_elem == this);
        stack_.deferred_comparison_.negated_stack_elem = nullptr;
    }

    void StackElem::insert_literal(Literal x)
    {
        MONAD_COMPILER_ASSERT(!literal_.has_value());
        literal_ = x;
    }

    void StackElem::insert_stack_offset(StackOffset x)
    {
        MONAD_COMPILER_ASSERT(!stack_offset_.has_value());
        stack_offset_ = x;
        auto removed = stack_.available_stack_offsets_.erase(x.offset);
        MONAD_COMPILER_ASSERT(removed == 1);
    }

    void StackElem::insert_avx_reg()
    {
        MONAD_COMPILER_ASSERT(!avx_reg_.has_value());
        auto x = stack_.free_avx_regs_.top();
        avx_reg_ = x;
        stack_.free_avx_regs_.pop();
        MONAD_COMPILER_ASSERT(stack_.avx_reg_stack_elems_[x.reg] == nullptr);
        stack_.avx_reg_stack_elems_[x.reg] = this;
    }

    void StackElem::insert_general_reg()
    {
        MONAD_COMPILER_ASSERT(!general_reg_.has_value());
        auto x = stack_.free_general_regs_.top();
        general_reg_ = x;
        stack_.free_general_regs_.pop();
        MONAD_COMPILER_ASSERT(
            stack_.general_reg_stack_elems_[x.reg] == nullptr);
        stack_.general_reg_stack_elems_[x.reg] = this;
    }

    void StackElem::free_avx_reg()
    {
        MONAD_COMPILER_ASSERT(avx_reg_.has_value());
        stack_.free_avx_regs_.push(avx_reg_.value());
        auto reg = avx_reg_->reg;
        MONAD_COMPILER_ASSERT(stack_.avx_reg_stack_elems_[reg] == this);
        stack_.avx_reg_stack_elems_[reg] = nullptr;
    }

    void StackElem::free_general_reg()
    {
        MONAD_COMPILER_ASSERT(general_reg_.has_value());
        stack_.free_general_regs_.push(general_reg_.value());
        auto reg = general_reg_->reg;
        MONAD_COMPILER_ASSERT(stack_.general_reg_stack_elems_[reg] != nullptr);
        stack_.general_reg_stack_elems_[reg] = nullptr;
    }

    void StackElem::free_stack_offset()
    {
        MONAD_COMPILER_ASSERT(stack_offset_.has_value());
        auto [_, inserted] =
            stack_.available_stack_offsets_.insert(stack_offset_->offset);
        MONAD_COMPILER_ASSERT(inserted);
    }

    void StackElem::remove_avx_reg()
    {
        free_avx_reg();
        avx_reg_ = std::nullopt;
    }

    void StackElem::remove_general_reg()
    {
        free_general_reg();
        general_reg_ = std::nullopt;
    }

    void StackElem::remove_stack_offset()
    {
        free_stack_offset();
        stack_offset_ = std::nullopt;
    }

    void StackElem::remove_literal()
    {
        literal_ = std::nullopt;
    }

    StackElem::~StackElem()
    {
        if (stack_offset_.has_value()) {
            free_stack_offset();
        }
        if (avx_reg_.has_value()) {
            free_avx_reg();
        }
        if (general_reg_.has_value()) {
            free_general_reg();
        }
        if (stack_.deferred_comparison_.stack_elem == this) {
            discharge_deferred_comparison();
        }
        if (stack_.deferred_comparison_.negated_stack_elem == this) {
            discharge_negated_deferred_comparison();
        }
    }

    Stack::Stack()
        : top_index_{-1}
        , min_delta_{0}
        , max_delta_{0}
        , delta_{0}
    {
        avx_reg_stack_elems_.fill(nullptr);
        general_reg_stack_elems_.fill(nullptr);
        for (std::uint8_t i = 0; i < AVX_REG_COUNT; ++i) {
            free_avx_regs_.emplace(i);
        }
        for (std::uint8_t i = 0; i < GENERAL_REG_COUNT; ++i) {
            free_general_regs_.emplace(i);
        }
    }

    Stack::Stack(basic_blocks::Block const &block)
        : Stack()
    {
        for (auto const &instr : block.instrs) {
            delta_ -= instr.stack_args();
            min_delta_ = std::min(delta_, min_delta_);

            // We need to treat SWAP and DUP slightly differently to other
            // instructions; they require that the minimum delta is adjusted to
            // ensure a big enough input stack, but because they don't actually
            // consume these elements, this change shouldn't be reflected in the
            // net delta.
            if (instr.opcode() == OpCode::Swap ||
                instr.opcode() == OpCode::Dup) {
                delta_ += instr.stack_args();
            }

            delta_ += instr.increases_stack();
            max_delta_ = std::max(delta_, max_delta_);
        }

        delta_ -= static_cast<int32_t>(
            basic_blocks::terminator_inputs(block.terminator));
        min_delta_ = std::min(delta_, min_delta_);

        for (auto i = -1; i >= min_delta_; --i) {
            StackElemRef e = std::make_shared<StackElem>(this);
            e->stack_offset_ = StackOffset{i};
            e->stack_indices_.insert(i);
            negative_elems_.push_back(std::move(e));
        }
        positive_elems_.resize(static_cast<size_t>(max_delta_));
        for (auto i = 0; i < max_delta_; ++i) {
            available_stack_offsets_.insert(i);
        }
    }

    StackElemRef Stack::get(std::int32_t index)
    {
        MONAD_COMPILER_ASSERT(index <= top_index_);
        return at(index);
    }

    StackElemRef &Stack::at(std::int32_t index)
    {
        if (index < 0) {
            auto i = static_cast<std::size_t>(-index - 1);
            MONAD_COMPILER_ASSERT(i < negative_elems_.size());
            return negative_elems_[i];
        }
        else {
            auto i = static_cast<std::size_t>(index);
            MONAD_COMPILER_ASSERT(i < positive_elems_.size());
            return positive_elems_[i];
        }
    }

    StackElemRef Stack::top()
    {
        return at(top_index_);
    }

    StackElemRef Stack::pop()
    {
        auto &e = at(top_index_);

        auto rem = e->stack_indices_.erase(top_index_);
        MONAD_COMPILER_ASSERT(rem == 1);

        // Note that it's valid for stack indices to become negative here.
        top_index_ -= 1;

        return std::move(e); // Move to prevent increment reference count
    }

    void Stack::push(StackElemRef e)
    {
        top_index_ += 1;
        auto [_, ins] = e->stack_indices_.insert(top_index_);
        MONAD_COMPILER_ASSERT(ins);
        at(top_index_) = std::move(e);
    }

    void Stack::push_deferred_comparison(Comparison c)
    {
        top_index_ += 1;
        auto e = std::make_shared<StackElem>(this);
        e->stack_indices_.insert(top_index_);
        e->deferred_comparison(c);
        at(top_index_) = std::move(e);
    }

    bool Stack::negate_top_deferred_comparison()
    {
        auto e = get(top_index_);
        auto &dc = deferred_comparison_;
        if (dc.stack_elem == e.get()) {
            pop();
            if (dc.negated_stack_elem) {
                auto i = dc.negated_stack_elem->stack_indices_.begin();
                MONAD_COMPILER_DEBUG_ASSERT(
                    i != dc.negated_stack_elem->stack_indices_.end());
                push(at(*i));
            }
            else {
                auto d = std::make_shared<StackElem>(this);
                d->negated_deferred_comparison();
                push(std::move(d));
            }
            return true;
        }
        else if (dc.negated_stack_elem == e.get()) {
            pop();
            if (dc.stack_elem) {
                auto i = dc.stack_elem->stack_indices_.begin();
                MONAD_COMPILER_DEBUG_ASSERT(
                    i != dc.stack_elem->stack_indices_.end());
                push(at(*i));
            }
            else {
                auto d = std::make_shared<StackElem>(this);
                d->deferred_comparison();
                push(std::move(d));
            }
            return true;
        }
        return false;
    }

    void Stack::push_literal(uint256_t const &x)
    {
        top_index_ += 1;
        auto e = alloc_literal(Literal{x});
        e->stack_indices_.insert(top_index_);
        at(top_index_) = std::move(e);
    }

    void Stack::dup(std::int32_t stack_index)
    {
        MONAD_COMPILER_ASSERT(stack_index <= top_index_);
        push(at(stack_index));
    }

    void Stack::swap(std::int32_t swap_index)
    {
        MONAD_COMPILER_ASSERT(swap_index < top_index_);

        auto t = top();
        auto &e = at(swap_index);

        auto rem_t = t->stack_indices_.erase(top_index_);
        MONAD_COMPILER_ASSERT(rem_t == 1);

        auto rem_e = e->stack_indices_.erase(swap_index);
        MONAD_COMPILER_ASSERT(rem_e == 1);

        auto ins_t = t->stack_indices_.insert(swap_index);
        MONAD_COMPILER_ASSERT(ins_t.second);

        auto ins_e = e->stack_indices_.insert(top_index_);
        MONAD_COMPILER_ASSERT(ins_e.second);

        at(top_index_) = std::move(e);
        e = std::move(t);
    }

    DeferredComparison Stack::discharge_deferred_comparison()
    {
        DeferredComparison dc{deferred_comparison_};
        if (dc.stack_elem) {
            dc.stack_elem->discharge_deferred_comparison();
            insert_stack_offset(dc.stack_elem);
        }
        if (dc.negated_stack_elem) {
            dc.negated_stack_elem->discharge_negated_deferred_comparison();
            insert_stack_offset(dc.negated_stack_elem);
        }
        return dc;
    }

    bool Stack::has_deferred_comparison() const
    {
        return deferred_comparison_.stack_elem != nullptr ||
               deferred_comparison_.negated_stack_elem != nullptr;
    }

    bool Stack::has_deferred_comparison_at(std::int32_t stack_index) const
    {
        auto const &dc = deferred_comparison_;
        if (dc.stack_elem &&
            dc.stack_elem->stack_indices_.contains(stack_index)) {
            return true;
        }
        if (dc.negated_stack_elem &&
            dc.negated_stack_elem->stack_indices_.contains(stack_index)) {
            return true;
        }
        return false;
    }

    StackOffset Stack::find_available_stack_offset(std::int32_t preferred) const
    {
        if (available_stack_offsets_.contains(preferred)) {
            return {preferred};
        }
        MONAD_COMPILER_ASSERT(!available_stack_offsets_.empty());
        return {*available_stack_offsets_.begin()};
    }

    std::optional<StackOffset> Stack::spill_avx_reg()
    {
        MONAD_COMPILER_ASSERT(free_avx_regs_.empty());
        for (std::uint8_t i = 0; i < AVX_REG_COUNT; ++i) {
            auto *e = avx_reg_stack_elems_[i];
            if (e == nullptr || e->reserve_avx_reg_count_ != 0) {
                continue;
            }
            return spill_avx_reg(e);
        }
        MONAD_COMPILER_ASSERT(false);
    }

    std::optional<StackOffset> Stack::spill_avx_reg(StackElemRef e)
    {
        return spill_avx_reg(e.get());
    }

    std::optional<StackOffset> Stack::spill_avx_reg(StackElem *e)
    {
        e->remove_avx_reg();
        if (e->stack_offset_.has_value() || e->general_reg_.has_value() ||
            e->literal_.has_value()) {
            return std::nullopt;
        }
        std::int32_t const preferred = e->preferred_stack_offset();
        StackOffset offset = find_available_stack_offset(preferred);
        e->insert_stack_offset(offset);
        return offset;
    }

    void Stack::unsafe_drop_avx_reg(StackElem *e)
    {
        e->remove_avx_reg();
    }

    void Stack::spill_stack_offset(StackElemRef e)
    {
        MONAD_COMPILER_ASSERT(
            e->avx_reg_.has_value() || e->general_reg_.has_value() ||
            e->literal_.has_value());
        e->remove_stack_offset();
    }

    void Stack::spill_literal(StackElemRef e)
    {
        MONAD_COMPILER_ASSERT(
            e->avx_reg_.has_value() || e->general_reg_.has_value() ||
            e->stack_offset_.has_value());
        e->remove_literal();
    }

    std::optional<StackOffset> Stack::spill_general_reg()
    {
        MONAD_COMPILER_ASSERT(free_general_regs_.empty());
        std::uint8_t best_index = 0;
        std::int8_t best_score = -1;
        for (std::uint8_t i = 0; i < GENERAL_REG_COUNT; ++i) {
            auto *e = general_reg_stack_elems_[i];
            if (e == nullptr || e->reserve_general_reg_count_ != 0) {
                continue;
            }
            std::int8_t score = 0;
            if (e->stack_offset_.has_value()) {
                score |= 0b100;
            }
            if (e->literal_.has_value()) {
                score |= 0b10;
            }
            if (e->avx_reg_.has_value()) {
                score |= 0b1;
            }
            if (score >= best_score) {
                best_score = score;
                best_index = i;
            }
        }

        MONAD_COMPILER_ASSERT(best_score >= 0);

        return spill_general_reg(general_reg_stack_elems_[best_index]);
    }

    std::optional<StackOffset> Stack::spill_general_reg(StackElemRef e)
    {
        return spill_general_reg(e.get());
    }

    std::optional<StackOffset> Stack::spill_general_reg(StackElem *e)
    {
        e->remove_general_reg();
        if (e->stack_offset_.has_value() || e->avx_reg_.has_value() ||
            e->literal_.has_value()) {
            return std::nullopt;
        }
        std::int32_t const preferred = e->preferred_stack_offset();
        StackOffset offset = find_available_stack_offset(preferred);
        e->insert_stack_offset(offset);
        return offset;
    }

    std::vector<std::pair<GeneralReg, StackOffset>>
    Stack::spill_all_caller_save_general_regs()
    {
        static_assert(CALLEE_SAVE_GENERAL_REG_ID == 0);
        std::vector<std::pair<GeneralReg, StackOffset>> ret;
        for (std::uint8_t i = 1; i < GENERAL_REG_COUNT; ++i) {
            auto *e = general_reg_stack_elems_[i];
            if (e == nullptr) {
                continue;
            }
            MONAD_COMPILER_ASSERT(e->reserve_general_reg_count_ == 0);
            GeneralReg const reg = e->general_reg_.value();
            e->remove_general_reg();
            if (!e->stack_offset_.has_value() && !e->avx_reg_.has_value() &&
                !e->literal_.has_value()) {
                std::int32_t const preferred = e->preferred_stack_offset();
                StackOffset const offset =
                    find_available_stack_offset(preferred);
                e->insert_stack_offset(offset);
                ret.emplace_back(reg, offset);
            }
        }
        return ret;
    }

    std::vector<std::pair<AvxReg, StackOffset>> Stack::spill_all_avx_regs()
    {
        std::vector<std::pair<AvxReg, StackOffset>> ret;
        for (std::uint8_t i = 0; i < AVX_REG_COUNT; ++i) {
            auto *e = avx_reg_stack_elems_[i];
            if (e == nullptr) {
                continue;
            }
            MONAD_COMPILER_ASSERT(e->reserve_avx_reg_count_ == 0);
            AvxReg const reg = e->avx_reg_.value();
            e->remove_avx_reg();
            if (!e->stack_offset_.has_value() && !e->general_reg_.has_value() &&
                !e->literal_.has_value()) {
                std::int32_t const preferred = e->preferred_stack_offset();
                StackOffset const offset =
                    find_available_stack_offset(preferred);
                e->insert_stack_offset(offset);
                ret.emplace_back(reg, offset);
            }
        }
        return ret;
    }

    std::set<std::int32_t> const &Stack::available_stack_offsets()
    {
        return available_stack_offsets_;
    }

    void Stack::insert_stack_offset(StackElemRef e, std::int32_t preferred)
    {
        insert_stack_offset(e.get(), preferred);
    }

    void Stack::insert_stack_offset(StackElemRef e)
    {
        insert_stack_offset(e.get());
    }

    void Stack::insert_stack_offset(StackElem *e, std::int32_t preferred)
    {
        if (e->stack_offset_.has_value()) {
            return;
        }
        auto offset = find_available_stack_offset(preferred);
        e->insert_stack_offset(offset);
    }

    void Stack::insert_stack_offset(StackElem *e)
    {
        std::int32_t const preferred = e->preferred_stack_offset();
        insert_stack_offset(e, preferred);
    }

    std::pair<AvxRegReserv, std::optional<StackOffset>>
    Stack::insert_avx_reg(StackElemRef e)
    {
        if (e->avx_reg_.has_value()) {
            return {AvxRegReserv{e}, std::nullopt};
        }
        std::optional<StackOffset> const spill_offset =
            free_avx_regs_.empty() ? spill_avx_reg() : std::nullopt;
        e->insert_avx_reg();
        return {AvxRegReserv{e}, spill_offset};
    }

    std::pair<GeneralRegReserv, std::optional<StackOffset>>
    Stack::insert_general_reg(StackElemRef e)
    {
        if (e->general_reg_.has_value()) {
            return {GeneralRegReserv{e}, std::nullopt};
        }
        std::optional<StackOffset> const spill_offset =
            free_general_regs_.empty() ? spill_general_reg() : std::nullopt;
        e->insert_general_reg();
        return {GeneralRegReserv{e}, spill_offset};
    }

    StackElemRef Stack::alloc_literal(Literal lit)
    {
        auto e = std::make_shared<StackElem>(this);
        e->insert_literal(lit);
        return e;
    }

    StackElemRef Stack::alloc_stack_offset(std::int32_t preferred)
    {
        auto e = std::make_shared<StackElem>(this);
        insert_stack_offset(e, preferred);
        return e;
    }

    std::tuple<StackElemRef, AvxRegReserv, std::optional<StackOffset>>
    Stack::alloc_avx_reg()
    {
        auto e = std::make_shared<StackElem>(this);
        auto [reserv, spill] = insert_avx_reg(e);
        return {std::move(e), reserv, spill};
    }

    std::tuple<StackElemRef, GeneralRegReserv, std::optional<StackOffset>>
    Stack::alloc_general_reg()
    {
        auto e = std::make_shared<StackElem>(this);
        auto [reserv, spill] = insert_general_reg(e);
        return {std::move(e), reserv, spill};
    }

    StackElemRef Stack::release_stack_offset(StackElemRef elem)
    {
        auto dst = std::make_shared<StackElem>(this);
        dst->stack_offset_ = elem->stack_offset_;
        elem->stack_offset_ = std::nullopt;
        return dst;
    }

    StackElemRef Stack::release_avx_reg(StackElemRef elem)
    {
        auto dst = std::make_shared<StackElem>(this);
        AvxReg const reg = elem->avx_reg_.value();
        dst->avx_reg_ = reg;
        elem->avx_reg_ = std::nullopt;
        avx_reg_stack_elems_[reg.reg] = dst.get();
        return dst;
    }

    StackElemRef Stack::release_general_reg(StackElemRef elem)
    {
        auto dst = std::make_shared<StackElem>(this);
        GeneralReg const reg = elem->general_reg_.value();
        dst->general_reg_ = reg;
        elem->general_reg_ = std::nullopt;
        general_reg_stack_elems_[reg.reg] = dst.get();
        return dst;
    }

    bool Stack::is_general_reg_on_stack(GeneralReg reg)
    {
        auto *e = general_reg_stack_elems_[reg.reg];
        return e != nullptr && e->is_on_stack();
    }

    std::int32_t Stack::min_delta() const
    {
        return min_delta_;
    }

    std::int32_t Stack::max_delta() const
    {
        return max_delta_;
    }

    std::int32_t Stack::delta() const
    {
        return delta_;
    }

    std::int32_t Stack::top_index() const
    {
        return top_index_;
    }
}
