#include "duckdb/storage/write_ahead_log.hpp"

#include "duckdb/catalog/catalog_entry/macro_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/view_catalog_entry.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/parser/parsed_data/alter_table_info.hpp"

#include <cstring>

namespace duckdb {

WriteAheadLog::WriteAheadLog(DatabaseInstance &database) : initialized(false), database(database) {
}

void WriteAheadLog::Initialize(string &path) {
	writer = make_unique<BufferedFileWriter>(database.GetFileSystem(), path.c_str(),
	                                         FileFlags::FILE_FLAGS_WRITE | FileFlags::FILE_FLAGS_FILE_CREATE |
	                                             FileFlags::FILE_FLAGS_APPEND);
	initialized = true;
}

int64_t WriteAheadLog::GetWALSize() {
	return writer->GetFileSize();
}

void WriteAheadLog::Truncate(int64_t size) {
	writer->Truncate(size);
}
//===--------------------------------------------------------------------===//
// Write Entries
//===--------------------------------------------------------------------===//
//===--------------------------------------------------------------------===//
// CREATE TABLE
//===--------------------------------------------------------------------===//
void WriteAheadLog::WriteCreateTable(TableCatalogEntry *entry) {
	writer->Write<WALType>(WALType::CREATE_TABLE);
	entry->Serialize(*writer);
}

//===--------------------------------------------------------------------===//
// DROP TABLE
//===--------------------------------------------------------------------===//
void WriteAheadLog::WriteDropTable(TableCatalogEntry *entry) {
	writer->Write<WALType>(WALType::DROP_TABLE);
	writer->WriteString(entry->schema->name);
	writer->WriteString(entry->name);
}

//===--------------------------------------------------------------------===//
// CREATE SCHEMA
//===--------------------------------------------------------------------===//
void WriteAheadLog::WriteCreateSchema(SchemaCatalogEntry *entry) {
	writer->Write<WALType>(WALType::CREATE_SCHEMA);
	writer->WriteString(entry->name);
}

//===--------------------------------------------------------------------===//
// SEQUENCES
//===--------------------------------------------------------------------===//
void WriteAheadLog::WriteCreateSequence(SequenceCatalogEntry *entry) {
	writer->Write<WALType>(WALType::CREATE_SEQUENCE);
	entry->Serialize(*writer);
}

void WriteAheadLog::WriteDropSequence(SequenceCatalogEntry *entry) {
	writer->Write<WALType>(WALType::DROP_SEQUENCE);
	writer->WriteString(entry->schema->name);
	writer->WriteString(entry->name);
}

void WriteAheadLog::WriteSequenceValue(SequenceCatalogEntry *entry, SequenceValue val) {
	writer->Write<WALType>(WALType::SEQUENCE_VALUE);
	writer->WriteString(entry->schema->name);
	writer->WriteString(entry->name);
	writer->Write<uint64_t>(val.usage_count);
	writer->Write<int64_t>(val.counter);
}

//===--------------------------------------------------------------------===//
// MACRO'S
//===--------------------------------------------------------------------===//
void WriteAheadLog::WriteCreateMacro(MacroCatalogEntry *entry) {
	writer->Write<WALType>(WALType::CREATE_MACRO);
	entry->Serialize(*writer);
}

void WriteAheadLog::WriteDropMacro(MacroCatalogEntry *entry) {
	writer->Write<WALType>(WALType::DROP_MACRO);
	writer->WriteString(entry->schema->name);
	writer->WriteString(entry->name);
}

//===--------------------------------------------------------------------===//
// VIEWS
//===--------------------------------------------------------------------===//
void WriteAheadLog::WriteCreateView(ViewCatalogEntry *entry) {
	writer->Write<WALType>(WALType::CREATE_VIEW);
	entry->Serialize(*writer);
}

void WriteAheadLog::WriteDropView(ViewCatalogEntry *entry) {
	writer->Write<WALType>(WALType::DROP_VIEW);
	writer->WriteString(entry->schema->name);
	writer->WriteString(entry->name);
}

//===--------------------------------------------------------------------===//
// DROP SCHEMA
//===--------------------------------------------------------------------===//
void WriteAheadLog::WriteDropSchema(SchemaCatalogEntry *entry) {
	writer->Write<WALType>(WALType::DROP_SCHEMA);
	writer->WriteString(entry->name);
}

//===--------------------------------------------------------------------===//
// DATA
//===--------------------------------------------------------------------===//
void WriteAheadLog::WriteSetTable(string &schema, string &table) {
	writer->Write<WALType>(WALType::USE_TABLE);
	writer->WriteString(schema);
	writer->WriteString(table);
}

void WriteAheadLog::WriteInsert(DataChunk &chunk) {
	D_ASSERT(chunk.size() > 0);
	chunk.Verify();

	writer->Write<WALType>(WALType::INSERT_TUPLE);
	chunk.Serialize(*writer);
}

void WriteAheadLog::WriteDelete(DataChunk &chunk) {
	D_ASSERT(chunk.size() > 0);
	D_ASSERT(chunk.ColumnCount() == 1 && chunk.data[0].type == LOGICAL_ROW_TYPE);
	chunk.Verify();

	writer->Write<WALType>(WALType::DELETE_TUPLE);
	chunk.Serialize(*writer);
}

void WriteAheadLog::WriteUpdate(DataChunk &chunk, column_t col_idx) {
	D_ASSERT(chunk.size() > 0);
	chunk.Verify();

	writer->Write<WALType>(WALType::UPDATE_TUPLE);
	writer->Write<column_t>(col_idx);
	chunk.Serialize(*writer);
}

//===--------------------------------------------------------------------===//
// Write ALTER Statement
//===--------------------------------------------------------------------===//
void WriteAheadLog::WriteAlter(AlterInfo &info) {
	writer->Write<WALType>(WALType::ALTER_INFO);
	info.Serialize(*writer);
}

//===--------------------------------------------------------------------===//
// FLUSH
//===--------------------------------------------------------------------===//
void WriteAheadLog::Flush() {
	// write an empty entry
	writer->Write<WALType>(WALType::WAL_FLUSH);
	// flushes all changes made to the WAL to disk
	writer->Sync();
}

} // namespace duckdb
