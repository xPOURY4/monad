#pragma once

#include "kind.h"

#include <compiler/transactional_unordered_map.h>

#include <unordered_set>

namespace monad::compiler::poly_typed
{
    class SubstMap
    {
        TransactionalUnorderedMap<VarName, LiteralType> literal_map;
        TransactionalUnorderedMap<VarName, std::unordered_set<VarName>>
            literal_links;
        TransactionalUnorderedMap<VarName, ContKind> cont_map;
        TransactionalUnorderedMap<VarName, Kind> kind_map;

    public:
        SubstMap();

        void link_literal_vars(VarName, VarName);

        void insert_literal_type(VarName, LiteralType);

        void insert_cont(VarName v, ContKind c)
        {
            cont_map.put(v, std::move(c));
        }

        void insert_kind(VarName v, Kind k)
        {
            kind_map.put(v, std::move(k));
        }

        // Throws DepthException and TickException
        ContKind subst(ContKind);

        // Throws DepthException and TickException
        Kind subst(Kind);

        ContKind subst_to_var(ContKind);

        Kind subst_to_var(Kind);

        void transaction();

        void commit();

        void revert();

    private:
        ContKind subst2(ContKind, size_t, size_t &);
        Kind subst2(Kind, size_t, size_t &);
    };
}
