#include "kind.h"

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
}
