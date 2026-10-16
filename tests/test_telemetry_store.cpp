// =============================================================================
// test_telemetry_store.cpp
//
// Unit tests for TelemetryStore in src/telemetry_store.cpp.
//
// Most tests use the special path ":memory:", which makes SQLite keep the
// whole database in RAM. Nothing touches the disk, so the tests are fast and
// leave no files behind. Two tests use a real temporary file, to prove that
// data really is saved and can be read back after closing.
// =============================================================================
#include <gtest/gtest.h>

#include <unistd.h>  // getpid

#include <cstdio>
#include <string>

#include "telemetry_store.hpp"

namespace {

using namespace telemetry;

TelemetryFrame make_frame(std::uint32_t sequence_number) {
    TelemetryFrame frame;
    frame.sequence_number = sequence_number;
    frame.timestamp_milliseconds = 1'700'000'000'000ULL + sequence_number;
    frame.health_state = HealthState::Nominal;
    frame.reading_is_fresh = true;
    frame.reading.temperature_centi_celsius = 2200;
    frame.reading.pressure_pascals = 101325;
    frame.reading.humidity_centi_percent = 4500;
    return frame;
}

// Returns a file path that is unique to this test process and test name.
std::string temporary_database_path(const std::string& test_name) {
    return testing::TempDir() + "telemetry_store_" + test_name + "_" +
           std::to_string(::getpid()) + ".db";
}

TEST(TelemetryStore, OpensInMemoryDatabase) {
    TelemetryStore store;

    ASSERT_TRUE(store.open(":memory:", 10)) << store.last_error();

    EXPECT_TRUE(store.is_open());
    EXPECT_EQ(store.count_rows_in_database(), 0);
}

TEST(TelemetryStore, StoresFramesAndCountsThem) {
    TelemetryStore store;
    ASSERT_TRUE(store.open(":memory:", 10));

    for (std::uint32_t i = 0; i < 25; ++i) {
        ASSERT_TRUE(store.store_frame(make_frame(i))) << store.last_error();
    }
    ASSERT_TRUE(store.commit_pending_frames());

    EXPECT_EQ(store.frames_stored(), 25u);
    EXPECT_EQ(store.count_rows_in_database(), 25);
}

TEST(TelemetryStore, RejectsZeroFramesPerCommit) {
    TelemetryStore store;

    EXPECT_FALSE(store.open(":memory:", 0));
    EXPECT_FALSE(store.is_open());
    EXPECT_FALSE(store.last_error().empty());
}

TEST(TelemetryStore, StoreFrameFailsWhenNotOpen) {
    TelemetryStore store;

    EXPECT_FALSE(store.store_frame(make_frame(0)));
    EXPECT_EQ(store.count_rows_in_database(), -1);
}

TEST(TelemetryStore, OpenFailsForUnwritablePath) {
    TelemetryStore store;

    // A file cannot be created inside a directory that does not exist.
    EXPECT_FALSE(store.open("/this/directory/does/not/exist/telemetry.db", 10));
    EXPECT_FALSE(store.is_open());
    EXPECT_FALSE(store.last_error().empty());
}

TEST(TelemetryStore, CloseIsSafeToCallTwice) {
    TelemetryStore store;
    ASSERT_TRUE(store.open(":memory:", 10));

    store.close();
    store.close();

    EXPECT_FALSE(store.is_open());
}

// Data must survive closing and reopening the file, including frames that
// were still waiting in an unfinished batch when close() was called.
TEST(TelemetryStore, DataPersistsAfterCloseAndReopen) {
    const std::string path = temporary_database_path("persist");
    std::remove(path.c_str());

    {
        TelemetryStore store;
        ASSERT_TRUE(store.open(path, 10)) << store.last_error();
        // 13 frames with a batch size of 10: one full batch of 10 is committed
        // automatically, and 3 are left waiting.
        for (std::uint32_t i = 0; i < 13; ++i) {
            ASSERT_TRUE(store.store_frame(make_frame(i)));
        }
        store.close();  // must commit the 3 waiting frames
    }

    TelemetryStore reopened;
    ASSERT_TRUE(reopened.open(path, 10)) << reopened.last_error();
    EXPECT_EQ(reopened.count_rows_in_database(), 13);

    reopened.close();
    std::remove(path.c_str());
}

// Proves the batching really happens: a second, separate connection to the
// same file can only see rows that have been committed.
TEST(TelemetryStore, FramesBecomeVisibleOnlyWhenTheBatchCommits) {
    const std::string path = temporary_database_path("batching");
    std::remove(path.c_str());

    TelemetryStore writer;
    TelemetryStore reader;
    ASSERT_TRUE(writer.open(path, 5)) << writer.last_error();
    ASSERT_TRUE(reader.open(path, 5)) << reader.last_error();

    for (std::uint32_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(writer.store_frame(make_frame(i)));
    }
    EXPECT_EQ(reader.count_rows_in_database(), 0) << "4 of 5: batch not committed yet";

    ASSERT_TRUE(writer.store_frame(make_frame(4)));
    EXPECT_EQ(reader.count_rows_in_database(), 5) << "5 of 5: batch committed";

    writer.close();
    reader.close();
    std::remove(path.c_str());
}

// Reopening an existing database must add to it, not erase it.
TEST(TelemetryStore, ReopeningAppendsToExistingData) {
    const std::string path = temporary_database_path("append");
    std::remove(path.c_str());

    for (int run = 0; run < 2; ++run) {
        TelemetryStore store;
        ASSERT_TRUE(store.open(path, 10));
        for (std::uint32_t i = 0; i < 5; ++i) {
            ASSERT_TRUE(store.store_frame(make_frame(i)));
        }
        store.close();
    }

    TelemetryStore reopened;
    ASSERT_TRUE(reopened.open(path, 10));
    EXPECT_EQ(reopened.count_rows_in_database(), 10);

    reopened.close();
    std::remove(path.c_str());
}

}  // namespace
