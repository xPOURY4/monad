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

    ContKind cont_kind(std::vector<Kind> kinds, ContTailKind t);

    ContKind cont_kind(std::vector<Kind> kinds, VarName v);

    ContKind cont_kind(std::vector<Kind> kinds);

    struct Word
    {
    };

    extern Kind word;

    struct Any
    {
    };

    extern Kind any;

    struct KindVar
    {
        VarName var;
    };

    Kind kind_var(VarName);

    struct LiteralVar
    {
        VarName var;
        ContKind cont;
    };

    Kind literal_var(VarName, ContKind);

    struct WordCont
    {
        ContKind cont;
    };

    Kind word_cont(ContKind);

    struct Cont
    {
        ContKind cont;
    };

    Kind cont(ContKind);

    enum class LiteralType
    {
        Word,
        Cont,
        WordCont
    };
}
