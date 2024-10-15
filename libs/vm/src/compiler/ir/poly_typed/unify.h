#pragma once

#include "strongly_connected_components.h"
#include "subst_map.h"

namespace monad::compiler::poly_typed
{
    bool unify(SubstMap &, Kind, Kind);

    bool unify(SubstMap &, ContKind, ContKind);
}
