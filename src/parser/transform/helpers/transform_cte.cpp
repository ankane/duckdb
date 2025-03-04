#include "duckdb/parser/transformer.hpp"
#include "duckdb/common/enums/set_operation_type.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/query_node/recursive_cte_node.hpp"

namespace duckdb {

using namespace duckdb_libpgquery;

void Transformer::TransformCTE(PGWithClause *de_with_clause, SelectStatement &select) {
	// TODO: might need to update in case of future lawsuit
	D_ASSERT(de_with_clause);

	D_ASSERT(de_with_clause->ctes);
	for (auto cte_ele = de_with_clause->ctes->head; cte_ele != NULL; cte_ele = cte_ele->next) {
		auto info = make_unique<CommonTableExpressionInfo>();

		auto cte = reinterpret_cast<PGCommonTableExpr *>(cte_ele->data.ptr_value);
		if (cte->aliascolnames) {
			for (auto node = cte->aliascolnames->head; node != nullptr; node = node->next) {
				info->aliases.push_back(reinterpret_cast<PGValue *>(node->data.ptr_value)->val.str);
			}
		}
		// lets throw some errors on unsupported features early
		if (cte->ctecolnames) {
			throw NotImplementedException("Column name setting not supported in CTEs");
		}
		if (cte->ctecoltypes) {
			throw NotImplementedException("Column type setting not supported in CTEs");
		}
		if (cte->ctecoltypmods) {
			throw NotImplementedException("Column type modification not supported in CTEs");
		}
		if (cte->ctecolcollations) {
			throw NotImplementedException("CTE collations not supported");
		}
		// we need a query
		if (!cte->ctequery || cte->ctequery->type != T_PGSelectStmt) {
			throw Exception("A CTE needs a SELECT");
		}

		// CTE transformation can either result in inlining for non recursive CTEs, or in recursive CTE bindings
		// otherwise.
		if (cte->cterecursive || de_with_clause->recursive) {
			info->query = TransformRecursiveCTE(cte, *info);
		} else {
			info->query = TransformSelect(cte->ctequery);
		}

		if (!info->query) {
			throw Exception("A CTE needs a SELECT");
		}
		auto cte_name = string(cte->ctename);

		auto it = select.cte_map.find(cte_name);
		if (it != select.cte_map.end()) {
			// can't have two CTEs with same name
			throw Exception("A CTE needs an unique name");
		}
		select.cte_map[cte_name] = move(info);
	}
}

unique_ptr<SelectStatement> Transformer::TransformRecursiveCTE(PGCommonTableExpr *cte,
                                                               CommonTableExpressionInfo &info) {
	auto stmt = (PGSelectStmt *)cte->ctequery;

	unique_ptr<SelectStatement> select;
	switch (stmt->op) {
	case PG_SETOP_UNION:
	case PG_SETOP_EXCEPT:
	case PG_SETOP_INTERSECT: {
		select = make_unique<SelectStatement>();
		select->node = make_unique_base<QueryNode, RecursiveCTENode>();
		auto result = (RecursiveCTENode *)select->node.get();
		result->ctename = string(cte->ctename);
		result->union_all = stmt->all;
		result->left = TransformSelectNode(stmt->larg);
		result->right = TransformSelectNode(stmt->rarg);
		result->aliases = info.aliases;

		if (!result->left || !result->right) {
			throw Exception("Failed to transform recursive CTE children.");
		}

		bool select_distinct = true;
		switch (stmt->op) {
		case PG_SETOP_UNION:
			// We don't need a DISTINCT operation on top of a recursive UNION CTE.
			select_distinct = false;
			break;
		default:
			throw Exception("Unexpected setop type for recursive CTE");
		}
		// if we compute the distinct result here, we do not have to do this in
		// the children. This saves a bunch of unnecessary DISTINCTs.
		if (select_distinct) {
			result->modifiers.push_back(make_unique<DistinctModifier>());
		}
		break;
	}
	default:
		// This CTE is not recursive. Fallback to regular query transformation.
		return TransformSelect(cte->ctequery);
	}

	if (stmt->limitCount || stmt->limitOffset) {
		throw ParserException("LIMIT or OFFSET in a recursive query is not allowed");
	}
	if (stmt->sortClause) {
		throw ParserException("ORDER BY in a recursive query is not allowed");
	}
	return select;
}

} // namespace duckdb
