//===----------------------------------------------------------------------===//
//                         DuckDB
//
// tpch-extension.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#define DUCKDB_BUILD_LOADABLE_EXTENSION
#include "duckdb.hpp"

namespace duckdb {

class TPCHExtension : public Extension {
public:
	void Load(DuckDB &db) override;
	std::string Name() override;

	//! Gets the specified TPC-H Query number as a string
	static std::string GetQuery(int query);
	//! Returns the CSV answer of a TPC-H query
	static std::string GetAnswer(double sf, int query);
};

} // namespace duckdb
