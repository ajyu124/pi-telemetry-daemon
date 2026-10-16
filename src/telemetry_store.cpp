// =============================================================================
// telemetry_store.cpp
//
// Implementation of TelemetryStore using SQLite's C API. See
// telemetry_store.hpp for what SQLite, transactions and prepared statements
// are, and why writes are batched.
//
// SQLite C API in one paragraph: sqlite3_open opens a file. sqlite3_exec runs
// a simple SQL string. For repeated inserts you sqlite3_prepare_v2 a statement
// containing `?` placeholders once, then for each row sqlite3_bind_* the
// values, sqlite3_step to run it, and sqlite3_reset to reuse it.
// =============================================================================
#include "telemetry_store.hpp"

#include <sqlite3.h>

namespace telemetry {

namespace {

const char* const kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS telemetry_frames ("
    "  row_id                    INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  sequence_number           INTEGER NOT NULL,"
    "  timestamp_milliseconds    INTEGER NOT NULL,"
    "  health_state              INTEGER NOT NULL,"
    "  reading_is_fresh          INTEGER NOT NULL,"
    "  consecutive_read_failures INTEGER NOT NULL,"
    "  temperature_centi_celsius INTEGER NOT NULL,"
    "  pressure_pascals          INTEGER NOT NULL,"
    "  humidity_centi_percent    INTEGER NOT NULL"
    ");";

// The eight `?` marks are placeholders, filled in by sqlite3_bind_* below.
// Placeholders are numbered starting at 1, not 0.
const char* const kInsertFrameSql =
    "INSERT INTO telemetry_frames ("
    "  sequence_number, timestamp_milliseconds, health_state, reading_is_fresh,"
    "  consecutive_read_failures, temperature_centi_celsius, pressure_pascals,"
    "  humidity_centi_percent"
    ") VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

}  // namespace

TelemetryStore::~TelemetryStore() {
    close();
}

bool TelemetryStore::execute_sql(const char* sql) {
    char* error_message = nullptr;
    const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &error_message);
    if (result != SQLITE_OK) {
        last_error_ = error_message != nullptr ? error_message : "unknown SQLite error";
        sqlite3_free(error_message);
        return false;
    }
    return true;
}

bool TelemetryStore::open(const std::string& database_path, std::uint32_t frames_per_commit) {
    close();

    if (frames_per_commit == 0) {
        last_error_ = "frames_per_commit must be at least 1";
        return false;
    }
    frames_per_commit_ = frames_per_commit;

    if (sqlite3_open(database_path.c_str(), &database_) != SQLITE_OK) {
        last_error_ = "could not open database '" + database_path + "': " +
                      (database_ != nullptr ? sqlite3_errmsg(database_) : "out of memory");
        // SQLite requires sqlite3_close even after a failed open.
        sqlite3_close(database_);
        database_ = nullptr;
        return false;
    }

    if (!execute_sql(kCreateTableSql)) {
        last_error_ = "could not create table: " + last_error_;
        sqlite3_close(database_);
        database_ = nullptr;
        return false;
    }

    if (sqlite3_prepare_v2(database_, kInsertFrameSql, -1, &insert_statement_, nullptr) !=
        SQLITE_OK) {
        last_error_ = std::string("could not prepare insert: ") + sqlite3_errmsg(database_);
        sqlite3_close(database_);
        database_ = nullptr;
        insert_statement_ = nullptr;
        return false;
    }

    frames_stored_ = 0;
    frames_in_open_transaction_ = 0;
    transaction_is_open_ = false;
    last_error_.clear();
    return true;
}

bool TelemetryStore::store_frame(const TelemetryFrame& frame) {
    if (!is_open()) {
        last_error_ = "store_frame called before open";
        return false;
    }

    // Start a new batch if one is not already in progress.
    if (!transaction_is_open_) {
        if (!execute_sql("BEGIN TRANSACTION;")) {
            return false;
        }
        transaction_is_open_ = true;
        frames_in_open_transaction_ = 0;
    }

    // Fill in the eight placeholders, in the same order as kInsertFrameSql.
    sqlite3_bind_int64(insert_statement_, 1, static_cast<sqlite3_int64>(frame.sequence_number));
    sqlite3_bind_int64(insert_statement_, 2,
                       static_cast<sqlite3_int64>(frame.timestamp_milliseconds));
    sqlite3_bind_int(insert_statement_, 3, static_cast<int>(frame.health_state));
    sqlite3_bind_int(insert_statement_, 4, frame.reading_is_fresh ? 1 : 0);
    sqlite3_bind_int(insert_statement_, 5, static_cast<int>(frame.consecutive_read_failures));
    sqlite3_bind_int64(insert_statement_, 6,
                       static_cast<sqlite3_int64>(frame.reading.temperature_centi_celsius));
    sqlite3_bind_int64(insert_statement_, 7,
                       static_cast<sqlite3_int64>(frame.reading.pressure_pascals));
    sqlite3_bind_int64(insert_statement_, 8,
                       static_cast<sqlite3_int64>(frame.reading.humidity_centi_percent));

    // Run the insert. SQLITE_DONE means "finished with no rows to return",
    // which is the success result for an INSERT.
    const int step_result = sqlite3_step(insert_statement_);
    // Reset so the statement can be reused for the next frame.
    sqlite3_reset(insert_statement_);

    if (step_result != SQLITE_DONE) {
        last_error_ = std::string("insert failed: ") + sqlite3_errmsg(database_);
        return false;
    }

    ++frames_stored_;
    ++frames_in_open_transaction_;

    if (frames_in_open_transaction_ >= frames_per_commit_) {
        return commit_pending_frames();
    }
    return true;
}

bool TelemetryStore::commit_pending_frames() {
    if (!is_open() || !transaction_is_open_) {
        return true;  // nothing waiting
    }
    const bool committed = execute_sql("COMMIT;");
    transaction_is_open_ = false;
    frames_in_open_transaction_ = 0;
    return committed;
}

std::int64_t TelemetryStore::count_rows_in_database() {
    if (!is_open()) {
        return -1;
    }

    sqlite3_stmt* count_statement = nullptr;
    if (sqlite3_prepare_v2(database_, "SELECT COUNT(*) FROM telemetry_frames;", -1,
                           &count_statement, nullptr) != SQLITE_OK) {
        last_error_ = std::string("count failed: ") + sqlite3_errmsg(database_);
        return -1;
    }

    std::int64_t row_count = -1;
    // SQLITE_ROW means "one row of results is ready to read".
    if (sqlite3_step(count_statement) == SQLITE_ROW) {
        row_count = sqlite3_column_int64(count_statement, 0);
    }
    sqlite3_finalize(count_statement);
    return row_count;
}

void TelemetryStore::close() {
    if (database_ == nullptr) {
        return;
    }
    commit_pending_frames();
    // Prepared statements must be released before the database is closed.
    sqlite3_finalize(insert_statement_);
    insert_statement_ = nullptr;
    sqlite3_close(database_);
    database_ = nullptr;
}

}  // namespace telemetry
