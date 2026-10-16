# Code walkthrough

A reading order for the code, with time estimates and three hands-on exercises. The goal is that by the end you can explain every design choice without notes.

There are about 1,500 lines of C++ and 1,200 lines of tests. You do not need all of it on day one. Part 1 and Part 2 below cover the core, about 600 lines, and are enough to talk about the project. Part 3 can wait until the hardware arrives.

**Tip for every file:** read the `.hpp` first. Its opening comment explains the purpose, the vocabulary and the reasoning. Then skim the `.cpp`. Then open the matching test file, because tests show how the code is meant to be used.

## Before you start (15 minutes)

1. Run `bash scripts/run_demo.sh` and read the "What you should see" table in `README.md`.
2. Run the two programs by hand in two terminals (see "Run it by hand" in `README.md`). Stop the viewer with `Ctrl+C` while the daemon is running and notice the daemon does not care. Start the viewer again and it picks the stream back up at the current sequence number. Then do the opposite: stop and restart the daemon while the viewer keeps running, and the viewer reports `SEQUENCE RESTARTED`, because the count began again at 0.

## Part 1. The message (about 45 minutes)

Start with the data, because everything else exists to produce or carry it.

| Order | File | What to take away |
|---|---|---|
| 1 | `src/sensor_reading.hpp` | The three measurements, and why they are integers with the unit in the name. |
| 2 | `src/telemetry_frame.hpp` | The 34-byte layout table. This is the "protocol". Learn what each field is for: sync bytes, sequence number, fresh flag, CRC. |
| 3 | `src/telemetry_frame.cpp` | How a number becomes bytes with shifts (`>> 8`, `& 0xFF`), and the order of the five checks in `decode_frame`. |
| 4 | `src/crc16.hpp`, `src/crc16.cpp` | What a CRC is. You do not need to derive the maths. Know what it catches and that `"123456789"` must give `0x29B1`. |
| 5 | `tests/test_telemetry_frame.cpp` | Read `EncodedBytesMatchDocumentedLayout`. It shows the exact bytes for a known frame. |

**Check yourself.** Without looking: why is the CRC checked before the version field? (Answer: if the bytes are corrupted, the version field might be the corrupted part, so nothing inside the frame can be trusted until the CRC passes.)

## Part 2. The loop (about 75 minutes)

| Order | File | What to take away |
|---|---|---|
| 6 | `src/sensor_interface.hpp` | What an interface is, what `std::optional` means, and why this one file makes the project testable. |
| 7 | `src/simulated_sensor.hpp`, `.cpp` | How the fake data and the fault window work. |
| 8 | `src/health_monitor.hpp`, `.cpp` | The state diagram. Trace it by hand: 5 failures, then 9 successes, then 1 failure, then 10 successes. What state after each? Check against `tests/test_health_monitor.cpp`. |
| 9 | `src/fixed_rate_timer.hpp`, `.cpp` | Why "sleep for 20 ms" drifts and "sleep until the next deadline" does not. Why a monotonic clock. |
| 10 | `src/telemetry_daemon.hpp`, `.cpp` | `execute_cycle()` is the whole program in 40 lines. Read it slowly. Then `run()`. |
| 11 | `src/udp_sender.hpp`, `.cpp` | What a socket is, UDP against TCP, and RAII (the destructor closes the socket). |
| 12 | `src/telemetry_store.hpp`, `.cpp` | Why writes are batched into transactions, and what is lost if power fails. |
| 13 | `src/main.cpp` | How the objects are created and handed to the daemon, and how `Ctrl+C` is handled safely. |
| 14 | `tests/test_telemetry_daemon.cpp` | `ScriptedSensor` and `FullFaultAndRecoverySequence`. This is the payoff of file 6. |

**Check yourself.** When the sensor fails, the daemon keeps sending frames. Why is that better than going quiet? (Answer: the receiver can tell "sensor down" from "link down". Silence would make them look the same.)

## Part 3. The hardware side (about 45 minutes, can wait)

Read this when the Raspberry Pi arrives, alongside `docs/RASPBERRY_PI_SETUP.md`.

| Order | File | What to take away |
|---|---|---|
| 15 | `src/i2c_bus_interface.hpp` | What I2C and registers are. |
| 16 | `src/bme280_sensor.hpp` | The three steps: read calibration once, read raw values, apply the datasheet formulas. |
| 17 | `src/bme280_sensor.cpp` | `configure_chip()` and `read_sensor()`. Treat the three `compensate_*` functions as copied from the datasheet. Nobody is expected to derive them. |
| 18 | `tests/fake_i2c_bus.hpp`, `tests/test_bme280_sensor.cpp` | How a driver is tested with no chip. |
| 19 | `src/linux_i2c_bus.cpp` | The only code that touches hardware: `open`, `ioctl`, `read`, `write`. |

## Part 4. The Python side (about 20 minutes)

| Order | File | What to take away |
|---|---|---|
| 20 | `tools/telemetry_frame.py` | The same frame layout expressed as one `struct` format string. |
| 21 | `tools/telemetry_viewer.py` | `count_lost_frames()` and `sequence_went_backwards()`: how sequence numbers reveal loss, and how a daemon restart is told apart from loss. |
| 22 | `tests/test_end_to_end.py` | How a test launches a real program and judges it from outside. |

## Part 5. The build (about 15 minutes)

Read `CMakeLists.txt` top to bottom. It is commented line by line. The one idea to keep: the logic is built as a library (`telemetry_core`), and both the program and the tests link against that same library.

## Three exercises

Doing these is the fastest way to make the project yours. Each one breaks something on purpose, so you can see a test catch it. After each exercise, undo your change and confirm everything passes again.

### Exercise 1. Break the checksum (5 minutes)

In `src/crc16.cpp`, change `kCrcPolynomial` from `0x1021` to `0x1022`. Run `bash scripts/build_and_test.sh`.

- `Crc16.MatchesPublishedCheckValue` and `Crc16.SingleByteKnownValue` fail, because the results no longer match the standard.
- `end_to_end_test` fails too. The C++ round-trip tests still pass, because C++ agrees with itself. Only the independent Python decoder notices. That is why a second implementation is worth having.

To see why a single test failed in detail, run it alone:

```bash
./build/unit_tests --gtest_filter='Crc16.*'
```

### Exercise 2. Reintroduce timing drift (5 minutes)

In `src/fixed_rate_timer.cpp`, replace the whole body of `wait_for_next_tick()` with the naive version:

```cpp
std::this_thread::sleep_for(period_);
```

Run the tests. `FixedRateTimer.WorkTimeDoesNotCauseDrift` fails: ten cycles that should take 200 ms take about 300 ms. You have now seen drift measured, which is a much better interview story than describing it. Two more timer tests fail as well, because the naive version has no idea what a missed deadline is.

### Exercise 3. Change the fault rule (10 minutes)

In `src/health_monitor.hpp`, change `failures_until_fault` from `5` to `3`. Before running anything, predict which tests will fail (there are five). Then run them and compare. Finally run `bash scripts/run_demo.sh` and find the line where the state becomes `FAULT`. It was `seq=154`. What is it now, and why?

### Going further

If you want a change that is entirely your own to talk about, good small ones are:

- Add a `--failures-until-fault` command line option (touch `daemon_config.*`, `main.cpp`, and add tests).
- Add a fourth measurement to the frame, such as a battery voltage from the simulated sensor. This walks you through every layer: struct, encoder, decoder, Python decoder, database column, tests, and the protocol version number.
- Make the viewer write what it receives to a CSV file.
