#include "subst_map.h"
#include "exceptions.h"

namespace
{
    size_t constexpr max_depth = 50;
}

namespace monad::compiler::poly_typed
{
    void SubstMap::link_literal_vars(VarName, VarName)
    {
    }

    void SubstMap::insert_literal_type(VarName, LiteralType)
    {
    }

    ContKind SubstMap::subst2(ContKind cont, size_t depth)
    {
        if (++depth > max_depth) {
            throw TickException{};
        }
        std::vector<Kind> kinds = cont->front;
        ContTailKind t = cont->tail;
        while (std::holds_alternative<ContVar>(t)) {
            auto new_c = cont_map.find(std::get<ContVar>(t).var);
            if (new_c == cont_map.end()) {
                break;
            }
            kinds.insert(kinds.end(), new_c->second->front.begin(), new_c->second->front.end());
            t = new_c->second->tail;
        }
        for (size_t i = 0; i < kinds.size(); ++i) {
            kinds[i] = subst2(kinds[i], depth);
        }
        return cont_kind(std::move(kinds), t);
    }

    Kind SubstMap::subst2(Kind kind, size_t depth)
    {
        if (++depth > max_depth) {
            throw TickException{};
        }

        while (std::holds_alternative<KindVar>(*kind)) {
            auto new_k = kind_map.find(std::get<KindVar>(*kind).var);
            if (new_k == kind_map.end()) {
                return kind;
            }
            kind = new_k->second;
        }

        return std::visit(Cases{
            [](Word const&) {
                return word;
            },
            [](Any const&) {
                return any;
            },
            [](KindVar const &) -> Kind {
                // unreachable
                std::terminate();
            },
            [this, depth](LiteralVar const &lv) {
                auto t = literal_map.find(lv.var);
                if (t == literal_map.end()) {
                    return literal_var(lv.var, lv.cont);
                }
                switch (t->second) {
                case LiteralType::Cont:
                    return cont(subst2(lv.cont, depth));
                case LiteralType::WordCont:
                    return word_cont(subst2(lv.cont, depth));
                case LiteralType::Word:
                    return word;
                }
            },
            [this, depth](WordCont const &wc) {
                return word_cont(subst2(wc.cont, depth));
            },
            [this, depth](Cont const &c) {
                return cont(subst2(c.cont, depth));
            }
        }, *kind);
    }

    ContKind SubstMap::subst(ContKind c)
    {
        return subst2(std::move(c), 0);
    }

    Kind SubstMap::subst(Kind k)
    {
        return subst2(std::move(k), 0);
    }

    ContKind SubstMap::subst_to_var(ContKind c)
    {
        return c;
    }

    Kind SubstMap::subst_to_var(Kind k)
    {
        return k;
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
