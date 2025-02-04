#pragma once

#include <monad/compiler/ir/poly_typed/strongly_connected_components.h>
#include <monad/compiler/ir/poly_typed/subst_map.h>

namespace monad::compiler::poly_typed
{
    void unify(SubstMap &, Kind, Kind);

    void unify(SubstMap &, ContKind, ContKind);

    using ParamVarNameMap = std::unordered_map<uint64_t, std::vector<VarName>>;

    void unify_param_var_name_map(
        SubstMap &, std::vector<VarName> const &, ParamVarNameMap const &);
}
