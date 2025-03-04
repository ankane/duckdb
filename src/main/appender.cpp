#include "duckdb/main/appender.hpp"

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/storage/data_table.hpp"

#include "duckdb/common/operator/cast_operators.hpp"

namespace duckdb {

Appender::Appender(Connection &con, string schema_name, string table_name) : context(con.context), column(0) {
	description = con.TableInfo(schema_name, table_name);
	if (!description) {
		// table could not be found
		throw CatalogException(StringUtil::Format("Table \"%s.%s\" could not be found", schema_name, table_name));
	} else {
		vector<LogicalType> types;
		for (auto &column : description->columns) {
			types.push_back(column.type);
		}
		chunk.Initialize(types);
	}
}

Appender::Appender(Connection &con, string table_name) : Appender(con, DEFAULT_SCHEMA, table_name) {
}

Appender::~Appender() {
	// flush any remaining chunks
	// wrapped in a try/catch because Close() can throw if the table was dropped in the meantime
	try {
		Close();
	} catch (...) {
	}
}

void Appender::BeginRow() {
}

void Appender::EndRow() {
	// check that all rows have been appended to
	if (column != chunk.ColumnCount()) {
		throw InvalidInputException("Call to EndRow before all rows have been appended to!");
	}
	column = 0;
	chunk.SetCardinality(chunk.size() + 1);
	if (chunk.size() >= STANDARD_VECTOR_SIZE) {
		Flush();
	}
}

template <class SRC, class DST> void Appender::AppendValueInternal(Vector &col, SRC input) {
	FlatVector::GetData<DST>(col)[chunk.size()] = Cast::Operation<SRC, DST>(input);
}

template <class T> void Appender::AppendValueInternal(T input) {
	if (column >= chunk.ColumnCount()) {
		throw InvalidInputException("Too many appends for chunk!");
	}
	auto &col = chunk.data[column];
	switch (col.type.InternalType()) {
	case PhysicalType::BOOL:
		AppendValueInternal<T, bool>(col, input);
		break;
	case PhysicalType::INT8:
		AppendValueInternal<T, int8_t>(col, input);
		break;
	case PhysicalType::INT16:
		AppendValueInternal<T, int16_t>(col, input);
		break;
	case PhysicalType::INT32:
		AppendValueInternal<T, int32_t>(col, input);
		break;
	case PhysicalType::INT64:
		AppendValueInternal<T, int64_t>(col, input);
		break;
	case PhysicalType::FLOAT:
		AppendValueInternal<T, float>(col, input);
		break;
	case PhysicalType::DOUBLE:
		AppendValueInternal<T, double>(col, input);
		break;
	default:
		AppendValue(Value::CreateValue<T>(input));
		return;
	}
	column++;
}

template <> void Appender::Append(bool value) {
	AppendValueInternal<bool>(value);
}

template <> void Appender::Append(int8_t value) {
	AppendValueInternal<int8_t>(value);
}

template <> void Appender::Append(int16_t value) {
	AppendValueInternal<int16_t>(value);
}

template <> void Appender::Append(int32_t value) {
	AppendValueInternal<int32_t>(value);
}

template <> void Appender::Append(int64_t value) {
	AppendValueInternal<int64_t>(value);
}

template <> void Appender::Append(const char *value) {
	AppendValueInternal<string_t>(string_t(value));
}

void Appender::Append(const char *value, uint32_t length) {
	AppendValueInternal<string_t>(string_t(value, length));
}

template <> void Appender::Append(float value) {
	if (!Value::FloatIsValid(value)) {
		throw InvalidInputException("Float value is out of range!");
	}
	AppendValueInternal<float>(value);
}

template <> void Appender::Append(double value) {
	if (!Value::DoubleIsValid(value)) {
		throw InvalidInputException("Double value is out of range!");
	}
	AppendValueInternal<double>(value);
}

template <> void Appender::Append(Value value) {
	if (column >= chunk.ColumnCount()) {
		throw InvalidInputException("Too many appends for chunk!");
	}
	AppendValue(move(value));
}

template <> void Appender::Append(std::nullptr_t value) {
	if (column >= chunk.ColumnCount()) {
		throw InvalidInputException("Too many appends for chunk!");
	}
	auto &col = chunk.data[column++];
	FlatVector::SetNull(col, chunk.size(), true);
}

void Appender::AppendValue(Value value) {
	chunk.SetValue(column, chunk.size(), value);
	column++;
}

void Appender::Flush() {
	// check that all vectors have the same length before appending
	if (column != 0) {
		throw InvalidInputException("Failed to Flush appender: incomplete append to row!");
	}

	if (chunk.size() == 0) {
		return;
	}
	context->Append(*description, chunk);

	chunk.Reset();
	column = 0;
}

void Appender::Close() {
	if (column == 0 || column == chunk.ColumnCount()) {
		Flush();
	}
}

} // namespace duckdb
