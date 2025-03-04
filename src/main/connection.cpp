#include "duckdb/main/connection.hpp"

#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/appender.hpp"
#include "duckdb/main/relation/read_csv_relation.hpp"
#include "duckdb/main/relation/table_relation.hpp"
#include "duckdb/main/relation/table_function_relation.hpp"
#include "duckdb/main/relation/value_relation.hpp"
#include "duckdb/main/relation/view_relation.hpp"
#include "duckdb/execution/operator/persistent/buffered_csv_reader.hpp"
#include "duckdb/parser/parser.hpp"

namespace duckdb {

Connection::Connection(DatabaseInstance &database) : context(make_shared<ClientContext>(database.shared_from_this())) {
#ifdef DEBUG
	EnableProfiling();
#endif
}

Connection::Connection(DuckDB &database) : Connection(*database.instance) {
}

string Connection::GetProfilingInformation(ProfilerPrintFormat format) {
	if (format == ProfilerPrintFormat::JSON) {
		return context->profiler.ToJSON();
	} else {
		return context->profiler.ToString();
	}
}

void Connection::Interrupt() {
	context->Interrupt();
}

void Connection::EnableProfiling() {
	context->EnableProfiling();
}

void Connection::DisableProfiling() {
	context->DisableProfiling();
}

void Connection::EnableQueryVerification() {
	context->query_verification_enabled = true;
}

void Connection::DisableQueryVerification() {
	context->query_verification_enabled = false;
}

void Connection::ForceParallelism() {
	context->force_parallelism = true;
}

unique_ptr<QueryResult> Connection::SendQuery(string query) {
	return context->Query(query, true);
}

unique_ptr<MaterializedQueryResult> Connection::Query(string query) {
	auto result = context->Query(query, false);
	D_ASSERT(result->type == QueryResultType::MATERIALIZED_RESULT);
	return unique_ptr_cast<QueryResult, MaterializedQueryResult>(move(result));
}

unique_ptr<MaterializedQueryResult> Connection::Query(unique_ptr<SQLStatement> statement) {
	auto result = context->Query(move(statement), false);
	D_ASSERT(result->type == QueryResultType::MATERIALIZED_RESULT);
	return unique_ptr_cast<QueryResult, MaterializedQueryResult>(move(result));
}

unique_ptr<PreparedStatement> Connection::Prepare(string query) {
	return context->Prepare(query);
}

unique_ptr<PreparedStatement> Connection::Prepare(unique_ptr<SQLStatement> statement) {
	return context->Prepare(move(statement));
}

unique_ptr<QueryResult> Connection::QueryParamsRecursive(string query, vector<Value> &values) {
	auto statement = Prepare(query);
	if (!statement->success) {
		return make_unique<MaterializedQueryResult>(statement->error);
	}
	return statement->Execute(values, false);
}

unique_ptr<TableDescription> Connection::TableInfo(string table_name) {
	return TableInfo(DEFAULT_SCHEMA, table_name);
}

unique_ptr<TableDescription> Connection::TableInfo(string schema_name, string table_name) {
	return context->TableInfo(schema_name, table_name);
}

vector<unique_ptr<SQLStatement>> Connection::ExtractStatements(string query) {
	return context->ParseStatements(query);
}

void Connection::Append(TableDescription &description, DataChunk &chunk) {
	context->Append(description, chunk);
}

shared_ptr<Relation> Connection::Table(string table_name) {
	return Table(DEFAULT_SCHEMA, move(table_name));
}

shared_ptr<Relation> Connection::Table(string schema_name, string table_name) {
	auto table_info = TableInfo(schema_name, table_name);
	if (!table_info) {
		throw Exception("Table does not exist!");
	}
	return make_shared<TableRelation>(*context, move(table_info));
}

shared_ptr<Relation> Connection::View(string tname) {
	return View(DEFAULT_SCHEMA, move(tname));
}

shared_ptr<Relation> Connection::View(string schema_name, string table_name) {
	return make_shared<ViewRelation>(*context, move(schema_name), move(table_name));
}

shared_ptr<Relation> Connection::TableFunction(string fname) {
	vector<Value> values;
	return TableFunction(move(fname), move(values));
}

shared_ptr<Relation> Connection::TableFunction(string fname, vector<Value> values) {
	return make_shared<TableFunctionRelation>(*context, move(fname), move(values));
}

shared_ptr<Relation> Connection::Values(vector<vector<Value>> values) {
	vector<string> column_names;
	return Values(move(values), move(column_names));
}

shared_ptr<Relation> Connection::Values(vector<vector<Value>> values, vector<string> column_names, string alias) {
	return make_shared<ValueRelation>(*context, move(values), move(column_names), alias);
}

shared_ptr<Relation> Connection::Values(string values) {
	vector<string> column_names;
	return Values(move(values), move(column_names));
}

shared_ptr<Relation> Connection::Values(string values, vector<string> column_names, string alias) {
	return make_shared<ValueRelation>(*context, move(values), move(column_names), alias);
}

shared_ptr<Relation> Connection::ReadCSV(string csv_file) {
	BufferedCSVReaderOptions options;
	options.file_path = csv_file;
	BufferedCSVReader reader(*context, options);
	vector<ColumnDefinition> column_list;
	for (idx_t i = 0; i < reader.sql_types.size(); i++) {
		column_list.push_back(ColumnDefinition(reader.col_names[i], reader.sql_types[i]));
	}
	return make_shared<ReadCSVRelation>(*context, csv_file, move(column_list), true);
}

shared_ptr<Relation> Connection::ReadCSV(string csv_file, vector<string> columns) {
	// parse columns
	vector<ColumnDefinition> column_list;
	for (auto &column : columns) {
		auto col_list = Parser::ParseColumnList(column);
		if (col_list.size() != 1) {
			throw ParserException("Expected a single column definition");
		}
		column_list.push_back(move(col_list[0]));
	}
	return make_shared<ReadCSVRelation>(*context, csv_file, move(column_list));
}

void Connection::BeginTransaction() {
	auto result = Query("BEGIN TRANSACTION");
	if (!result->success) {
		throw Exception(result->error);
	}
}

void Connection::Commit() {
	auto result = Query("COMMIT");
	if (!result->success) {
		throw Exception(result->error);
	}
}

void Connection::Rollback() {
	auto result = Query("ROLLBACK");
	if (!result->success) {
		throw Exception(result->error);
	}
}

void Connection::SetAutoCommit(bool auto_commit) {
	context->transaction.SetAutoCommit(auto_commit);
}

bool Connection::IsAutoCommit() {
	return context->transaction.IsAutoCommit();
}

} // namespace duckdb
