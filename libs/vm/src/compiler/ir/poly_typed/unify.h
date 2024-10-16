#pragma once

#include "strongly_connected_components.h"
#include "subst_map.h"

namespace monad::compiler::poly_typed
{
    bool unify(SubstMap &, Kind, Kind);

    bool unify(SubstMap &, ContKind, ContKind);

    using ParamVarNameMap = std::unordered_map<uint64_t, std::vector<VarName>>;

    bool unify_param_var_name_map(
        SubstMap &, std::vector<VarName> const &, ParamVarNameMap const &);
}
