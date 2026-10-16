# Design decisions

Every major choice in this project, the options that were considered, and why one was picked. Interviewers rarely ask "what does your code do". They ask "why did you do it that way, and what else could you have done". This file is the answer to that question, eleven times.

Each decision has the same layout: the question, the options with their good and bad points, the choice, and what it costs.

---

## 1. Where does it run? Linux, through WSL on Windows

**The question.** The development computer runs Windows. The eventual target is a Raspberry Pi, which runs Linux.

| Option | Good | Bad |
|---|---|---|
| **Linux only, run in WSL** (chosen) | Same operating system family as the Pi, so the same code, the same commands and the same build work in both places. Linux is what the job postings ask for. | One-time WSL install and a restart. |
| Native Windows build | Nothing to install. | Windows networking code (Winsock) differs from Linux. I2C access does not exist on Windows. The code would need two versions of several files, and the Pi version could never be compiled on the laptop. |
| Docker container | Fully reproducible. | Docker Desktop is a heavier install than WSL, and uses WSL underneath anyway. |
| GitHub Codespaces (Linux in a browser) | Zero install. | Needs the project on GitHub first, and an internet connection to work at all. |

**Choice.** Linux only, run in WSL. Codespaces is the documented fallback in `TROUBLESHOOTING.md` if WSL will not install.

**Cost.** The project cannot be built with Visual Studio on plain Windows.

---

## 2. How does the code talk to the sensor? Through an interface

**The question.** The daemon needs sensor data, but the real sensor is not available yet, and even when it is, tests should not depend on hardware.

| Option | Good | Bad |
|---|---|---|
| **Abstract interface with several implementations** (chosen) | The loop is written once against `SensorInterface`. A simulated sensor, the real driver and scripted test fakes all plug in. Swapping to real hardware changes one line in `main.cpp`. | A little more code, and a virtual function call per read (irrelevant at 50 Hz). |
| Call the hardware directly from the loop | Least code. | Cannot run or test without the chip attached. Fault handling cannot be tested without physically unplugging things. |
| `#ifdef SIMULATED` scattered through the code | No virtual calls. | Two programs hiding in one set of files. The code that is tested is not the code that ships. |

**Choice.** Two layers of interface: `SensorInterface` (any sensor) and `I2cBusInterface` (the wire the BME280 sits on). The second layer means even the BME280 driver's register logic is tested without a chip.

**Cost.** Roughly 100 extra lines. In exchange, every file except `src/linux_i2c_bus.cpp` is testable on a laptop.

**Term.** This boundary is called a **hardware abstraction layer (HAL)**.

---

## 3. Values on the wire: scaled integers, not floating point

**The question.** How should 22.57 degrees be represented in a message?

| Option | Good | Bad |
|---|---|---|
| **Scaled integers** (chosen): send 2257, meaning hundredths of a degree | One unambiguous byte layout. Exact comparisons in tests. Works on processors with no floating point hardware. Matches the BME280, which produces integers natively. | The unit and scale must be documented, and the receiver must divide. |
| IEEE 754 `float` | No scaling to remember. | Sender and receiver must agree on float format and byte order. Rounding makes exact test comparisons awkward. |
| Text such as `"22.57"` or JSON | Human readable. | Several times larger, slow to parse, and needs dynamic memory. |

**Choice.** Scaled integers, with the unit in every field name (`temperature_centi_celsius`).

**Cost.** Resolution is fixed at 0.01 degrees, which is finer than the sensor's accuracy anyway.

---

## 4. Turning a frame into bytes: one byte at a time, not a memory copy

**The question.** How do the fields of `TelemetryFrame` become 34 bytes?

| Option | Good | Bad |
|---|---|---|
| **Write each byte explicitly with shifts** (chosen) | The wire format is defined by the code, not by the compiler. Identical bytes on any CPU. No undefined behaviour. | About 40 lines of helper code. |
| Copy the struct's memory with `memcpy` and `#pragma pack` | Very short. | The compiler may insert invisible padding between fields. Byte order depends on the CPU. Reading a packed struct can cause misaligned access faults on some ARM processors. The format changes if someone reorders the struct. |
| A serialization library such as Protocol Buffers | Handles versioning and many languages. | A code generator, a build dependency, and a variable-length format with dynamic memory. Too heavy for a 34-byte fixed message. |

**Choice.** Explicit little-endian serialization in `src/telemetry_frame.cpp`.

**Cost.** Adding a field means editing the struct, the encoder, the decoder, the Python decoder and the layout table. The "golden bytes" test catches any mismatch.

**Why little-endian.** Both x86 laptops and ARM boards are little-endian natively, so it is the natural choice. Because the code uses shifts, it would still be correct on a big-endian machine.

---

## 5. Network transport: UDP, not TCP

**The question.** How do frames get from the device to the viewer?

| Option | Good | Bad |
|---|---|---|
| **UDP** (chosen) | No connection to set up. The sender never waits for the receiver. A lost frame is simply replaced by the next one 20 ms later. Each send is one whole message. | No delivery guarantee. Loss and reordering must be detected by the application. |
| TCP | Guaranteed, ordered delivery. | A lost packet stalls everything behind it while it is re-sent ("head-of-line blocking"), so the display shows old data late instead of new data on time. Needs connection handling and reconnection logic. TCP is a byte stream, so message boundaries must be reconstructed. |
| A message broker (MQTT, DDS) | Many subscribers, discovery, quality-of-service settings. DDS is common in defense systems. | A large dependency and configuration burden for one sender and one receiver. |

**Choice.** UDP. The frame's **sequence number** lets the receiver count lost frames, and the **CRC** rejects corrupted ones. So the two things TCP would have provided are handled, in the form this application actually needs.

**Cost.** If a frame is lost in transit, the viewer never sees it (the device's own SQLite copy still has it).

**Good follow-up to know.** DDS would be the natural next step for a multi-subscriber system, and it normally runs over UDP itself.

---

## 6. Error detection: CRC-16/CCITT-FALSE

**The question.** How does the receiver know a frame was not corrupted?

| Option | Good | Bad |
|---|---|---|
| **CRC-16/CCITT-FALSE** (chosen) | Detects every single-bit error, every double-bit error, and every burst of up to 16 bits in a message this size. Two bytes of overhead. Widely used in embedded and aerospace protocols, with a published test value. | Not cryptographic. It detects accidents, not tampering. |
| Simple sum of bytes | Trivial. | Misses swapped bytes and many multi-bit errors. |
| CRC-32 | Stronger. | Four bytes on a 34-byte frame, more than is needed. |
| A cryptographic hash or MAC | Detects deliberate tampering. | Large and slow, and needs key management. A different problem from line noise. |

**Choice.** CRC-16/CCITT-FALSE, computed bit by bit with no lookup table.

**Cost.** The bit-by-bit loop is slower than a 256-entry table, which does not matter for 1600 bytes per second. A table version is a well known optimisation if it ever did.

**Honest note.** UDP already carries its own 16-bit checksum, so on a real network the frame CRC is a second layer. It earns its place because the same frame format could be sent over a serial cable or radio link with no checksum of its own, and because the SQLite copy and any recorded file can be re-validated later.

---

## 7. Local storage: SQLite, committed once per second

**The question.** How are frames recorded on the device?

| Option | Good | Bad |
|---|---|---|
| **SQLite, batched commits** (chosen) | A real queryable database in one file, with no server. Survives crashes without corruption. Standard on embedded Linux. | Each commit waits for the disk. |
| Append raw frames to a binary file | Fastest and simplest. | Needs custom tooling to read. A crash can leave a half-written frame at the end. |
| CSV text file | Opens in a spreadsheet. | Large, slow, and still has the half-written-line problem. |
| Commit every frame individually | At most one frame lost on power failure. | Fifty disk syncs per second. Slow, and wears out an SD card. |

**Choice.** SQLite, with frames grouped into one transaction and committed once per second.

**Cost.** A power cut loses up to one second of recorded frames. That is a deliberate trade of durability for speed and SD card life, and the batch size is one constant if the trade should be different.

**Known limitation.** The commit happens on the same thread as the timing loop, so a slow disk can make one cycle late. The daemon counts these as missed deadlines. The fix would be a second thread that owns the database and receives frames through a queue. That was left out to keep the first version single-threaded and easy to reason about.

---

## 8. Loop timing: absolute deadlines on a monotonic clock

**The question.** How does the loop run at exactly 50 Hz?

| Option | Good | Bad |
|---|---|---|
| **Sleep until an absolute deadline, using `steady_clock`** (chosen) | The time the work takes is absorbed automatically. No drift, ever. Immune to the system clock being changed. | A little more thought than `sleep(20ms)`. |
| Do the work, then sleep for 20 ms | One line. | Each cycle is 20 ms plus the work time, so the real rate is below 50 Hz and the error accumulates. |
| Sleep until a deadline, using the wall clock | No drift. | If the clock is adjusted (network time sync), the loop stalls or fires a burst. |
| A hardware timer interrupt or a real-time OS | Guaranteed worst-case timing. | Not available to an ordinary Linux program. |

**Choice.** `std::this_thread::sleep_until` with deadlines that advance by exactly one period, on `std::chrono::steady_clock`. When a cycle overruns, the timer counts it and reschedules from now, with no catch-up burst.

**Cost.** This is soft real-time: steady on average, no guaranteed worst case. The daemon reports missed deadlines so the limitation is visible, not hidden. Options for tightening it on a Pi are in `RASPBERRY_PI_SETUP.md`.

---

## 9. Fault detection: a three-state machine with hysteresis

**The question.** What should happen when a sensor read fails?

| Option | Good | Bad |
|---|---|---|
| **Three states with slow recovery** (chosen) | One glitch is reported but not treated as an outage. A real outage is declared quickly (100 ms). An intermittent connection gives one steady FAULT, not flicker. | Three thresholds to choose. |
| Good or bad, decided per read | Simplest. | A loose wire makes the state flip many times a second. A single glitch looks the same as a dead sensor. |
| Stop sending when the sensor fails | Nothing to design. | The receiver cannot tell "sensor died" from "network died". This is the worst option. |
| Exit and let systemd restart the program | Clears any bad internal state. | Loses the stream for seconds, and does not help if the sensor is physically unplugged. |

**Choice.** `NOMINAL`, `DEGRADED` (1 failure), `FAULT` (5 failures in a row), with 10 good reads in a row required to leave `FAULT`. While failing, the daemon keeps sending frames on schedule, repeating the last good values with the "fresh" flag cleared.

**Cost.** After a real fault, good data is labelled `FAULT` for an extra 200 ms. The data itself is marked fresh during that time, so nothing is hidden.

**Term.** Requiring more evidence to leave a state than to enter it is called **hysteresis**.

---

## 10. Error handling: return values, not exceptions

**The question.** How do functions report failure?

| Option | Good | Bad |
|---|---|---|
| **`bool`, `std::optional` and status enums** (chosen) | Failure is visible in every function signature. Timing is predictable. Matches the common embedded practice of avoiding exceptions. | The caller must remember to check. |
| C++ exceptions | Errors cannot be silently ignored, and propagate automatically. | Cost when thrown is hard to bound, which matters in a timing loop. Many embedded and safety-critical coding standards forbid them. |

**Choice.** Return values everywhere. A failed sensor read is `std::nullopt`. A rejected frame carries a specific `DecodeStatus`. The loop path allocates no heap memory and throws nothing.

**Cost.** More `if` statements. In exchange, a sensor failure is an ordinary, tested path through the code and not an exceptional event.

---

## 11. Testing: GoogleTest for units, Python for the whole program

**The question.** How is correctness demonstrated?

| Option | Good | Bad |
|---|---|---|
| **GoogleTest unit tests plus a Python end-to-end test** (chosen) | GoogleTest is the most widely used C++ test framework. The Python test checks the real program from the outside with an independently written decoder. | Two languages. |
| Unit tests only | One language. | Proves the parts work, not that the program does. |
| Manual testing | No test code. | Not repeatable, and nothing to show a reviewer. |

**Choice.** 102 GoogleTest unit tests and 9 Python tests (4 of them end-to-end), all run by one command, and all run again under AddressSanitizer and UndefinedBehaviorSanitizer.

**How GoogleTest is obtained.** CMake first looks for a copy installed on the system (the setup script installs one, so the build needs no internet). If none is found it downloads a pinned version. A system copy builds fastest and works offline. The download fallback means the project still builds on a machine nobody has prepared.

**Cost.** Timing tests are the weak point of any test suite, because a busy machine can make them fail falsely. The timing tests here assert only lower bounds and very generous upper bounds.

---

## Things deliberately left out

Knowing what you chose not to build, and why, is as useful in an interview as what you did build.

| Left out | Why | What it would take |
|---|---|---|
| Multithreading | A single thread is far easier to reason about and to test, and 50 Hz does not need more. | A writer thread and a thread-safe queue for the database (see decision 7). |
| Encryption and authentication | Out of scope for a telemetry demo. The CRC is not a security feature. | An authenticated cipher such as AES-GCM, plus key distribution. |
| IPv6 and host names | Keeps the socket code short. | `getaddrinfo` in place of `inet_pton`. |
| Configuration file | Command line options are enough for a dozen settings. | A small parser, or a library. |
| Hard real-time guarantees | Not achievable in an ordinary Linux process. | A real-time kernel, `SCHED_FIFO` priority and locked memory, or a microcontroller with an RTOS. |
| Frame resynchronisation | UDP delivers whole frames, so the receiver never has to hunt for the start of one. | On a serial link, a byte-at-a-time parser that scans for the sync bytes. The sync bytes are already in the format for this reason. |
