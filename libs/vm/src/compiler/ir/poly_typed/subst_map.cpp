#include "subst_map.h"

#include "compiler/types.h"
#include "exceptions.h"
#include "kind.h"
#include <cassert>
#include <cstddef>
#include <exception>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace monad::compiler::poly_typed
{
    SubstMap::SubstMap()
        : literal_map{}
        , literal_links{}
        , cont_map{}
        , kind_map{}
    {
    }

    void SubstMap::link_literal_vars(VarName v1, VarName v2)
    {
        assert(!literal_map.contains(v1) && !literal_map.contains(v2));
        literal_links[v1].insert(v2);
        literal_links[v2].insert(v1);
    }

    void SubstMap::insert_literal_type(VarName v0, LiteralType t)
    {
        std::unordered_set<VarName> visited;
        std::vector<VarName> work_stack{v0};
        while (!work_stack.empty()) {
            VarName const v = work_stack.back();
            work_stack.pop_back();
            if (!visited.insert(v).second) {
                continue;
            }
            __attribute__((unused)) bool const ins = literal_map.put(v, t);
            assert(ins || t == LiteralType::Word);
            for (auto w : literal_links[v]) {
                work_stack.push_back(w);
            }
        }
    }

    ContKind SubstMap::subst2(ContKind cont, size_t depth, size_t &ticks)
    {
        increment_kind_depth(depth, 1);
        increment_kind_ticks(ticks, cont->front.size());

        std::vector<Kind> kinds = cont->front;
        ContTailKind t = cont->tail;
        while (std::holds_alternative<ContVar>(t)) {
            auto new_c = cont_map.find(std::get<ContVar>(t).var);
            if (new_c == cont_map.end()) {
                break;
            }
            increment_kind_ticks(ticks, 1 + new_c->second->front.size());
            kinds.insert(
                kinds.end(),
                new_c->second->front.begin(),
                new_c->second->front.end());
            t = new_c->second->tail;
        }
        for (auto &kind : kinds) {
            kind = subst2(kind, depth, ticks);
        }
        return cont_kind(std::move(kinds), t);
    }

    Kind SubstMap::subst2(Kind kind, size_t depth, size_t &ticks)
    {
        increment_kind_depth(depth, 1);

        while (std::holds_alternative<KindVar>(*kind)) {
            auto new_k = kind_map.find(std::get<KindVar>(*kind).var);
            if (new_k == kind_map.end()) {
                return kind;
            }
            increment_kind_ticks(ticks, 1);
            kind = new_k->second;
        }

        return std::visit(
            Cases{
                [](Word const &) { return word; },
                [](Any const &) { return any; },
                [](KindVar const &) -> Kind {
                    // unreachable
                    std::terminate();
                },
                [this, depth, &ticks](LiteralVar const &lv) {
                    auto t = literal_map.find(lv.var);
                    if (t == literal_map.end()) {
                        return literal_var(lv.var, lv.cont);
                    }
                    switch (t->second) {
                    case LiteralType::Cont:
                        increment_kind_ticks(ticks, 1);
                        return cont(subst2(lv.cont, depth, ticks));
                    case LiteralType::WordCont:
                        increment_kind_ticks(ticks, 1);
                        return word_cont(subst2(lv.cont, depth, ticks));
                    case LiteralType::Word:
                        return word;
                    }
                    std::terminate();
                },
                [this, depth, &ticks](WordCont const &wc) {
                    increment_kind_ticks(ticks, 1);
                    return word_cont(subst2(wc.cont, depth, ticks));
                },
                [this, depth, &ticks](Cont const &c) {
                    increment_kind_ticks(ticks, 1);
                    return cont(subst2(c.cont, depth, ticks));
                }},
            *kind);
    }

    ContKind SubstMap::subst(ContKind c)
    {
        size_t ticks = 0;
        return subst2(std::move(c), 0, ticks);
    }

    Kind SubstMap::subst(Kind k)
    {
        size_t ticks = 0;
        return subst2(std::move(k), 0, ticks);
    }

    ContKind SubstMap::subst_to_var(ContKind cont)
    {
        std::vector<Kind> kinds = cont->front;
        ContTailKind t = cont->tail;
        while (std::holds_alternative<ContVar>(t)) {
            auto new_c = cont_map.find(std::get<ContVar>(t).var);
            if (new_c == cont_map.end()) {
                break;
            }
            kinds.insert(
                kinds.end(),
                new_c->second->front.begin(),
                new_c->second->front.end());
            t = new_c->second->tail;
        }
        for (auto &kind : kinds) {
            kind = subst_to_var(kind);
        }
        return cont_kind(std::move(kinds), t);
    }

    Kind SubstMap::subst_to_var(Kind kind)
    {
        auto new_k = kind_map.find(std::get<KindVar>(*kind).var);
        while (new_k != kind_map.end() &&
               std::holds_alternative<KindVar>(*new_k->second)) {
            kind = new_k->second;
            new_k = kind_map.find(std::get<KindVar>(*kind).var);
        }
        return kind;
    }

    void SubstMap::transaction()
    {
        literal_map.transaction();
        literal_links.transaction();
        cont_map.transaction();
        kind_map.transaction();
    }

    void SubstMap::commit()
    {
        literal_map.commit();
        literal_links.commit();
        cont_map.commit();
        kind_map.commit();
    }

    void SubstMap::revert()
    {
        literal_map.revert();
        literal_links.revert();
        cont_map.revert();
        kind_map.revert();
    }
}
