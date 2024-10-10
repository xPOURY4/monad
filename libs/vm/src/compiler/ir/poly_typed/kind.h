#pragma once

#include <compiler/ir/poly_typed.h>
#include <compiler/transactional_unordered_map.h>

#include <memory>
#include <variant>
#include <vector>

namespace monad::compiler::poly_typed
{
    using VarName = uint64_t;

    struct Word;
    struct Any;
    struct KindVar;
    struct LiteralVar;
    struct WordCont;
    struct Cont;

    using PreKind =
        std::variant<Word, Any, KindVar, LiteralVar, WordCont, Cont>;

    using Kind = std::shared_ptr<PreKind>;

    struct ContVar
    {
        VarName var;
    };

    struct ContWords
    {
    };

    using ContTailKind = std::variant<ContVar, ContWords>;

    struct PreContKind
    {
        std::vector<Kind> front;
        ContTailKind tail;
    };

    using ContKind = std::shared_ptr<PreContKind>;

    struct Word
    {
    };

    struct Any
    {
    };

    struct KindVar
    {
        VarName var;
    };

    struct LiteralVar
    {
        VarName var;
        ContKind cont;
    };

    struct WordCont
    {
        ContKind cont;
    };

    struct Cont
    {
        ContKind cont;
    };

    struct SubstMap
    {
        // TODO
        // Kind variable map
        // ContKind variable map
        // Literal variable map
        // Literal variable links
    };
}
