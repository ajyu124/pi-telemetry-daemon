// =============================================================================
// telemetry_store.hpp
//
// PURPOSE
//   Saves every telemetry frame to a local SQLite database file, so there is
//   a permanent on-device record even when nobody is listening on the network.
//
// TERMS
//   SQLite:       a full SQL database that lives in one ordinary file and
//                 needs no server. It is built into phones, browsers and
//                 aircraft. Here the file is `telemetry.db` by default.
//   Transaction:  a group of database changes that are saved together as one
//                 unit. Finishing a transaction is called a "commit".
//   Prepared statement: an SQL command that is parsed once and then re-run
//                 many times with different values. Faster than re-parsing,
//                 and it rules out SQL injection.
//
// WHY BATCH THE WRITES?
//   Every commit makes SQLite wait until the data is physically on disk.
//   Committing 50 times a second would be slow and would wear out a Raspberry
//   Pi's SD card. Instead, frames are grouped into one transaction and
//   committed once per batch (once a second by default). The tradeoff: if
//   power is lost, up to one batch of frames that was not yet committed is
//   lost. See docs/DESIGN_DECISIONS.md, decision 7.
//
// THE TABLE
//   Table `telemetry_frames`, one row per frame:
//     row_id                     auto-numbered primary key
//     sequence_number            from the frame
//     timestamp_milliseconds     from the frame
//     health_state               0 NOMINAL, 1 DEGRADED, 2 FAULT
//     reading_is_fresh           1 or 0
//     consecutive_read_failures  from the frame
//     temperature_centi_celsius  0.01 C per count
//     pressure_pascals
//     humidity_centi_percent     0.01 % per count
//
//   You can inspect it yourself with:  sqlite3 telemetry.db
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "telemetry_frame.hpp"

// Forward declarations of SQLite's own types. This tells the compiler "these
// types exist" without including the large sqlite3.h header in every file
// that includes this one.
struct sqlite3;
struct sqlite3_stmt;

namespace telemetry {

class TelemetryStore {
public:
    TelemetryStore() = default;
    ~TelemetryStore();

    // Copying is disabled because this object owns a database connection.
    TelemetryStore(const TelemetryStore&) = delete;
    TelemetryStore& operator=(const TelemetryStore&) = delete;

    // Opens (or creates) the database file and creates the table if needed.
    //   database_path:     a file path, or ":memory:" for a temporary
    //                      in-memory database (used by the unit tests).
    //   frames_per_commit: how many frames to group into one transaction.
    //                      Must be at least 1.
    // Returns true on success. On failure, last_error() explains why.
    bool open(const std::string& database_path, std::uint32_t frames_per_commit);

    // Adds one frame. It becomes permanent at the next commit.
    bool store_frame(const TelemetryFrame& frame);

    // Commits any frames that are waiting. Called automatically by close().
    bool commit_pending_frames();

    // Commits pending frames and closes the database. Safe to call twice.
    void close();

    bool is_open() const { return database_ != nullptr; }

    // Frames passed to store_frame() successfully since open().
    std::uint64_t frames_stored() const { return frames_stored_; }

    // Total rows in the table, including rows from earlier runs. Returns -1
    // on error.
    std::int64_t count_rows_in_database();

    const std::string& last_error() const { return last_error_; }

private:
    bool execute_sql(const char* sql);

    sqlite3* database_ = nullptr;
    sqlite3_stmt* insert_statement_ = nullptr;
    std::uint32_t frames_per_commit_ = 1;
    std::uint32_t frames_in_open_transaction_ = 0;
    bool transaction_is_open_ = false;
    std::uint64_t frames_stored_ = 0;
    std::string last_error_;
};

}  // namespace telemetry
