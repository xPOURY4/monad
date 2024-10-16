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

        std::optional<LiteralType> get_literal_type(VarName);

        std::optional<Kind> get_kind(VarName);

        std::optional<ContKind> get_cont(VarName);

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

        std::optional<ContKind> subst(ContKind);

        std::optional<Kind> subst(Kind);

        /// Throws DepthException and TickException
        ContKind subst(ContKind, size_t depth, size_t &ticks);

        /// Throws DepthException and TickException
        Kind subst(Kind, size_t depth, size_t &ticks);

        std::vector<VarName> subst_to_var(ContKind);

        VarName subst_to_var(Kind);

        void transaction();

        void commit();

        void revert();

    private:
        VarName get_min_literal_var_name(VarName);
    };
}
