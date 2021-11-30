//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/optimizer/filter_combiner.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types/value.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/parser/expression_map.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/constant_filter.hpp"


#include "duckdb/storage/data_table.hpp"
#include <functional>

namespace duckdb {

enum class ValueComparisonResult { PRUNE_LEFT, PRUNE_RIGHT, UNSATISFIABLE_CONDITION, PRUNE_NOTHING };
enum class FilterResult { UNSATISFIABLE, SUCCESS, UNSUPPORTED };

//! The FilterCombiner combines several filters and generates a logically equivalent set that is more efficient
//! Amongst others:
//! (1) it prunes obsolete filter conditions: i.e. [X > 5 and X > 7] => [X > 7]
//! (2) it generates new filters for expressions in the same equivalence set: i.e. [X = Y and X = 500] => [Y = 500]
//! (3) it prunes branches that have unsatisfiable filters: i.e. [X = 5 AND X > 6] => FALSE, prune branch
class FilterCombiner {
public:
	struct ExpressionValueInformation {
		Value constant;
		ExpressionType comparison_type;
	};

	FilterResult AddFilter(unique_ptr<Expression> expr);

	void GenerateFilters(const std::function<void(unique_ptr<Expression> filter)> &callback);
	bool HasFilters();
	TableFilterSet GenerateTableScanFilters(vector<idx_t> &column_ids);
	// vector<unique_ptr<TableFilter>> GenerateZonemapChecks(vector<idx_t> &column_ids, vector<unique_ptr<TableFilter>>
	// &pushed_filters);

private:
	FilterResult AddFilter(Expression *expr);
	FilterResult AddBoundComparisonFilter(Expression *expr);
	FilterResult AddTransitiveFilters(BoundComparisonExpression &comparison);
	unique_ptr<Expression> FindTransitiveFilter(Expression *expr);
	// unordered_map<idx_t, std::pair<Value *, Value *>>
	// FindZonemapChecks(vector<idx_t> &column_ids, unordered_set<idx_t> &not_constants, Expression *filter);
	Expression *GetNode(Expression *expr);
	idx_t GetEquivalenceSet(Expression *expr);
	FilterResult AddConstantComparison(vector<ExpressionValueInformation> &info_list, ExpressionValueInformation info);

	//! Functions used to push and generate OR Filters
	void LookUpConjunctions(Expression *expr);
	void BFSLookUpConjunctions(BoundConjunctionExpression *conjunction);

	void UpdateConjunctionFilter(BoundComparisonExpression *comparison_expr);
	void UpdateFilterByColumn(BoundColumnRefExpression *column_ref, BoundComparisonExpression *comparison_expr, bool can_pushdown);
	void GenerateORFilters(TableFilterSet &table_filter, vector<idx_t> &column_ids);

	void SetCurrentConjunction(BoundConjunctionExpression *conjunction);
	bool CheckEarlyStopPushdown();

	template <typename CONJUNCTION_TYPE>
	void GenerateConjunctionFilter(BoundConjunctionExpression *conjunction, ConjunctionFilter *last_conj_filter) {
		auto new_filter = NextConjunctionFilter<CONJUNCTION_TYPE>(conjunction);
		auto conj_filter_ptr = (ConjunctionFilter *)new_filter.get();
		last_conj_filter->child_filters.push_back(move(new_filter));
		last_conj_filter = conj_filter_ptr;
	}

	template <typename CONJUNCTION_TYPE>
	unique_ptr<TableFilter> NextConjunctionFilter(BoundConjunctionExpression *conjunction) {
		unique_ptr<ConjunctionFilter> conj_filter = make_unique<CONJUNCTION_TYPE>();
		for (auto &expr: conjunction->children) {
			auto comp_expr = (BoundComparisonExpression *)expr.get();
			auto &const_expr = (comp_expr->left->type == ExpressionType::VALUE_CONSTANT) ? *comp_expr->left: *comp_expr->right;
			auto const_value = ExpressionExecutor::EvaluateScalar(const_expr);
			auto const_filter = make_unique<ConstantFilter>(comp_expr->type, const_value);
			conj_filter->child_filters.push_back(move(const_filter));
		}
		return conj_filter;
	}

private:
	vector<unique_ptr<Expression>> remaining_filters;

	expression_map_t<unique_ptr<Expression>> stored_expressions;
	unordered_map<Expression *, idx_t> equivalence_set_map;
	unordered_map<idx_t, vector<ExpressionValueInformation>> constant_values;
	unordered_map<idx_t, vector<Expression *>> equivalence_map;
	idx_t set_index = 0;


	//! Structures used for OR Filters

	// Structures to map a column reference to conjunction expressions
	struct ColConjunctionToPush {
		BoundColumnRefExpression *column_ref;

		// only preserve AND if there is a single column in the expression
		bool preserve_and = true;

		// flag to indicate if pushdown can conitnue, only if there are single comparisons, e.g., against scalar.
		// otherwise it will be false, e.g., in case of bound_functions
		bool can_pushdown = true;

		// conjunction chain for this column
		vector<unique_ptr<BoundConjunctionExpression>> conjunctions;
	};

	struct OrToPush {
		BoundConjunctionExpression *root_or;

		BoundConjunctionExpression *cur_conjunction;

		bool early_stop_pushdown = false;

		// pointer for the rela
		unique_ptr<ColConjunctionToPush> col_conjunction = nullptr;
	};

	vector<unique_ptr<OrToPush>> ors_to_pushdown;
};

} // namespace duckdb