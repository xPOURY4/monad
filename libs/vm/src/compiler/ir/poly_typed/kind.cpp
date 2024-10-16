#include "compiler/ir/poly_typed/kind.h"
#include "compiler/types.h"
#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    using namespace monad::compiler;
    using namespace monad::compiler::poly_typed;

    struct PolyVarSubstMap
    {
        std::unordered_map<VarName, VarName> kind_map;
        std::unordered_map<VarName, VarName> cont_map;
    };

    bool cont_alpha_eq(PolyVarSubstMap &, ContKind, ContKind);

    bool kind_alpha_eq(PolyVarSubstMap &su, Kind k1, Kind k2)
    {
        if (k1->index() != k2->index()) {
            return false;
        }
        return std::visit(
            Cases{
                [](Word const &) { return true; },
                [](Any const &) { return true; },
                [&su, &k2](KindVar const &kv1) {
                    KindVar const &kv2 = std::get<KindVar>(*k2);
                    auto it1 = su.kind_map.find(kv1.var);
                    auto it2 = su.kind_map.find(kv2.var);
                    if (it1 == su.kind_map.end() && it2 == su.kind_map.end()) {
                        su.kind_map.insert_or_assign(kv1.var, kv2.var);
                        su.kind_map.insert_or_assign(kv2.var, kv2.var);
                        return true;
                    }
                    else if (
                        it1 == su.kind_map.end() || it2 == su.kind_map.end()) {
                        return false;
                    }
                    return it1->second == it2->second;
                },
                [&su, &k2](LiteralVar const &lv1) {
                    LiteralVar const &lv2 = std::get<LiteralVar>(*k2);
                    if (lv1.var != lv2.var) {
                        return false;
                    }
                    return cont_alpha_eq(su, lv1.cont, lv2.cont);
                },
                [&su, &k2](WordCont const &wc1) {
                    WordCont const &wc2 = std::get<WordCont>(*k2);
                    return cont_alpha_eq(su, wc1.cont, wc2.cont);
                },
                [&su, &k2](Cont const &c1) {
                    Cont const &c2 = std::get<Cont>(*k2);
                    return cont_alpha_eq(su, c1.cont, c2.cont);
                }},
            *k1);
    }

    bool cont_alpha_eq(PolyVarSubstMap &su, ContKind c1, ContKind c2)
    {
        size_t const min_size = std::min(c1->front.size(), c2->front.size());
        for (size_t i = 0; i < min_size; ++i) {
            if (!kind_alpha_eq(su, c1->front[i], c2->front[i])) {
                return false;
            }
        }
        if (c1->tail.index() != c2->tail.index()) {
            return false;
        }
        return std::visit(
            Cases{
                [&su, &c1, &c2](ContVar const &cv1) {
                    if (c1->front.size() != c2->front.size()) {
                        return false;
                    }
                    ContVar const &cv2 = std::get<ContVar>(c2->tail);
                    auto it1 = su.cont_map.find(cv1.var);
                    auto it2 = su.cont_map.find(cv2.var);
                    if (it1 == su.cont_map.end() && it2 == su.cont_map.end()) {
                        su.cont_map.insert_or_assign(cv1.var, cv2.var);
                        su.cont_map.insert_or_assign(cv2.var, cv2.var);
                        return true;
                    }
                    else if (
                        it1 == su.cont_map.end() || it2 == su.cont_map.end()) {
                        return false;
                    }
                    return it1->second == it2->second;
                },
                [&su, &c1, &c2, min_size](ContWords const &) {
                    auto const &n = c1->front.size() > c2->front.size()
                                        ? c1->front
                                        : c2->front;
                    for (size_t i = min_size; i < n.size(); ++i) {
                        if (!kind_alpha_eq(su, n[i], word)) {
                            return false;
                        }
                    }
                    return true;
                }},
            c1->tail);
    }
}

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

    ContKind cont_words = cont_kind({});

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
                    std::format_to(ctx.out(), "L{} : ", lv.var);
                    format_cont(lv.cont, ctx);
                    if (use_parens) {
                        std::format_to(ctx.out(), ")");
                    }
                },
                [&ctx, use_parens](WordCont const &wc) {
                    if (use_parens) {
                        std::format_to(ctx.out(), "(");
                    }
                    std::format_to(ctx.out(), "Word : ");
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

    bool alpha_equal(Kind k1, Kind k2)
    {
        PolyVarSubstMap su;
        return kind_alpha_eq(su, std::move(k1), std::move(k2));
    }

    bool alpha_equal(ContKind c1, ContKind c2)
    {
        PolyVarSubstMap su;
        return cont_alpha_eq(su, std::move(c1), std::move(c2));
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
