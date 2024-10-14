#include "kind.h"
#include "compiler/types.h"
#include <cstddef>
#include <format>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monad::compiler::poly_typed
{
    Kind word = std::make_shared<PreKind>(Word{});

    Kind any = std::make_shared<PreKind>(Any{});

    Kind kind_var(VarName v)
    {
        return std::make_shared<PreKind>(KindVar{v});
    }

    Kind literal_var(VarName v, ContKind c)
    {
        return std::make_shared<PreKind>(LiteralVar{v, c});
    }

    Kind word_cont(ContKind c)
    {
        return std::make_shared<PreKind>(WordCont{c});
    }

    Kind cont(ContKind c)
    {
        return std::make_shared<PreKind>(Cont{c});
    }

    ContKind cont_kind(std::vector<Kind> kinds, ContTailKind t)
    {
        return std::make_shared<PreContKind>(std::move(kinds), t);
    }

    ContKind cont_kind(std::vector<Kind> kinds, VarName v)
    {
        return std::make_shared<PreContKind>(std::move(kinds), ContVar{v});
    }

    ContKind cont_kind(std::vector<Kind> kinds)
    {
        return std::make_shared<PreContKind>(std::move(kinds), ContWords{});
    }

    void format_cont(ContKind const &cont, std::format_context &ctx)
    {
        for (auto const &k : cont->front) {
            format_kind(k, ctx, true);
            std::format_to(ctx.out(), ",");
        }
        std::visit(
            Cases{
                [&ctx](ContVar const &cv) {
                    std::format_to(ctx.out(), "s{} -> Exit", cv.var);
                },
                [&ctx](ContWords const &) {
                    std::format_to(ctx.out(), "Word.. -> Exit");
                }},
            cont->tail);
    }

    void
    format_kind(Kind const &kind, std::format_context &ctx, bool use_parens)
    {
        std::visit(
            Cases{
                [&ctx](Word const &) { std::format_to(ctx.out(), "Word"); },
                [&ctx](Any const &) { std::format_to(ctx.out(), "Any"); },
                [&ctx](KindVar const &kv) {
                    std::format_to(ctx.out(), "v{}", kv.var);
                },
                [&ctx, use_parens](LiteralVar const &lv) {
                    if (use_parens) {
                        std::format_to(ctx.out(), "(");
                    }
                    std::format_to(ctx.out(), "L{} ~ ", lv.var);
                    format_cont(lv.cont, ctx);
                    if (use_parens) {
                        std::format_to(ctx.out(), ")");
                    }
                },
                [&ctx, use_parens](WordCont const &wc) {
                    if (use_parens) {
                        std::format_to(ctx.out(), "(");
                    }
                    std::format_to(ctx.out(), "Word ~ ");
                    format_cont(wc.cont, ctx);
                    if (use_parens) {
                        std::format_to(ctx.out(), ")");
                    }
                },
                [&ctx, use_parens](Cont const &c) {
                    if (use_parens) {
                        std::format_to(ctx.out(), "(");
                    }
                    format_cont(c.cont, ctx);
                    if (use_parens) {
                        std::format_to(ctx.out(), ")");
                    }
                }},
            *kind);
    }

    bool operator==(Kind k1, Kind k2)
    {
        if (k1->index() != k2->index()) {
            return false;
        }
        return std::visit(
            Cases{
                [](Word const &) { return true; },
                [](Any const &) { return true; },
                [&k2](KindVar const &kv1) {
                    KindVar const &kv2 = std::get<KindVar>(*k2);
                    return kv1.var == kv2.var;
                },
                [&k2](LiteralVar const &lv1) {
                    LiteralVar const &lv2 = std::get<LiteralVar>(*k2);
                    if (lv1.var != lv2.var) {
                        return false;
                    }
                    return lv1.cont == lv2.cont;
                },
                [&k2](WordCont const &wc1) {
                    WordCont const &wc2 = std::get<WordCont>(*k2);
                    return wc1.cont == wc2.cont;
                },
                [&k2](Cont const &c1) {
                    Cont const &c2 = std::get<Cont>(*k2);
                    return c1.cont == c2.cont;
                }},
            *k1);
    }

    bool operator==(ContKind c1, ContKind c2)
    {
        if (c1->front.size() != c2->front.size()) {
            return false;
        }
        for (size_t i = 0; i < c1->front.size(); ++i) {
            if (c1->front[i] != c2->front[i]) {
                return false;
            }
        }
        if (c1->tail.index() != c2->tail.index()) {
            return false;
        }
        return std::visit(
            Cases{
                [&c2](ContVar const &cv1) {
                    ContVar const &cv2 = std::get<ContVar>(c2->tail);
                    return cv1.var == cv2.var;
                },
                [](ContWords const &) { return true; }},
            c1->tail);
    }
}
