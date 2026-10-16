# Pi Telemetry Daemon

A C++17 program for Linux that reads a sensor at a fixed 50 times per second, packs each measurement into a small binary message protected by a checksum, streams it over the network with UDP, and records it in a local SQLite database. A Python viewer on the receiving end validates and displays the stream. If the sensor stops responding, a state machine detects the fault, flags the data as stale, and recovers when the sensor returns.

It runs today with a **simulated sensor**, so no hardware is needed. The driver for a real BME280 sensor on a Raspberry Pi is included and unit tested, and will be switched on when the hardware arrives.

```
   THE DEVICE (C++ daemon)                                THE GROUND STATION (Python)

 +--------+   +---------+   +---------+   +--------+   UDP    +-----------------------+
 | sensor |-->| health  |-->| build + |-->|  UDP   |--------->| telemetry_viewer.py   |
 | read   |   | state   |   | encode  |   | sender |  34-byte | checks CRC + sequence |
 +--------+   | machine |   | frame   |   +--------+  frames  | shows live values     |
     ^        +---------+   +---------+       |               +-----------------------+
     |                                        v
  50 times per second,               +----------------+
  locked to a monotonic clock        | SQLite database|
                                     +----------------+
```

## Contents

1. [Quick start on Windows](#quick-start-on-windows)
2. [What you should see](#what-you-should-see)
3. [Run it by hand](#run-it-by-hand)
4. [How the project is organised](#how-the-project-is-organised)
5. [Naming conventions](#naming-conventions)
6. [What is verified and what is not](#what-is-verified-and-what-is-not)
7. [Where to go next](#where-to-go-next)

## Quick start on Windows

This is a Linux program, because the Raspberry Pi and most defense and embedded systems run Linux. On Windows you run it inside **WSL** (Windows Subsystem for Linux), a feature built into Windows that gives you a real Ubuntu Linux terminal. If you get stuck at any step, open [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

### Step 1. Install WSL (one time, about 10 minutes)

Skip this step if you already have an "Ubuntu" app in your Start menu.

1. Click Start, type `PowerShell`, right-click **Windows PowerShell** and choose **Run as administrator**.
2. Type this and press Enter:
   ```
   wsl --install
   ```
3. Restart your computer when it asks.
4. After the restart an **Ubuntu** window opens by itself and finishes installing. It asks you to invent a Linux username and password. Pick anything simple. **Nothing appears on screen while you type the password. That is normal.**

### Step 2. Open the Ubuntu terminal

Click Start, type `Ubuntu`, and open it. Every command from here on is typed into this window.

### Step 3. Put the project inside Linux

Download `pi-telemetry-daemon.tar.gz` to your Windows Downloads folder, then run:

```bash
tar -xzf /mnt/c/Users/*/Downloads/pi-telemetry-daemon.tar.gz -C ~
cd ~/pi-telemetry-daemon
```

What that does: `/mnt/c/` is how Linux sees your Windows `C:` drive. `tar -xzf` unpacks the archive. `-C ~` puts it in your Linux home folder. `cd` moves into it.

### Step 4. Install the build tools (one time, about 3 minutes)

```bash
bash scripts/setup_ubuntu.sh
```

It asks for the Linux password you invented in step 1.

### Step 5. Build and test (about 1 minute)

```bash
bash scripts/build_and_test.sh
```

Success looks like this at the end:

```
100% tests passed, 0 tests failed out of 103
```

(That is the 102 C++ tests, plus the Python test file, which is counted as one entry here and contains 9 tests of its own.)

### Step 6. Run the demo (about 15 seconds)

```bash
bash scripts/run_demo.sh
```

### Later sessions

Each time you come back, open Ubuntu and run `cd ~/pi-telemetry-daemon`. To edit the code in VS Code, run `code .` from that folder (VS Code will offer to install its WSL extension the first time).

## What you should see

The demo runs for 10 seconds and breaks the simulated sensor for one second in the middle:

```
seq=99       state=NOMINAL  fresh=yes temp= 22.84 C  pressure=101334 Pa  humidity=46.40 %
seq=149      state=NOMINAL  fresh=yes temp= 23.12 C  pressure=101338 Pa  humidity=47.00 %
*** HEALTH STATE CHANGED: NOMINAL -> DEGRADED ***
seq=150      state=DEGRADED fresh=no  temp= 23.12 C  pressure=101338 Pa  humidity=47.00 %
*** HEALTH STATE CHANGED: DEGRADED -> FAULT ***
seq=154      state=FAULT    fresh=no  temp= 23.12 C  pressure=101338 Pa  humidity=47.00 %
seq=199      state=FAULT    fresh=no  temp= 23.12 C  pressure=101338 Pa  humidity=47.00 %
*** HEALTH STATE CHANGED: FAULT -> NOMINAL ***
seq=209      state=NOMINAL  fresh=yes temp= 23.58 C  pressure=101349 Pa  humidity=47.74 %
```

How to read it:

| Part | Meaning |
|---|---|
| `seq=150` | Sequence number. It goes up by one per frame, so the viewer can tell if any frame was lost on the way. |
| `state=` | The daemon's opinion of the sensor: `NOMINAL` (fine), `DEGRADED` (a read just failed), `FAULT` (5 reads in a row failed). |
| `fresh=no` | This frame repeats the last good measurement because the sensor did not answer. Notice the values freeze at 23.12 C. |
| `seq=154` | The fifth failure in a row (150, 151, 152, 153, 154), which is when DEGRADED becomes FAULT. |
| `seq=209` | The sensor came back at 200, but the daemon waits for 10 good reads in a row (200 to 209) before trusting it again. |

At the end the viewer prints `frames accepted: 500`, `frames rejected: 0`, `frames lost: 0`, `stale frames: 50`.

## Run it by hand

Running the two programs in separate windows makes it clearer which is which.

**Terminal 1** (the ground station). Open Ubuntu and run:

```bash
cd ~/pi-telemetry-daemon
python3 tools/telemetry_viewer.py
```

**Terminal 2** (the device). Open a second Ubuntu window and run:

```bash
cd ~/pi-telemetry-daemon
./build/telemetry_daemon
```

Press `Ctrl+C` in each window to stop. Things to try:

```bash
# See every option.
./build/telemetry_daemon --help

# Run for 10 seconds with a 1 second sensor fault starting at second 3.
./build/telemetry_daemon --frame-limit 500 --fault-start-frame 150 --fault-length-frames 50

# Run faster.
./build/telemetry_daemon --rate-hz 200

# Print every single frame in the viewer.
python3 tools/telemetry_viewer.py --print-every 1

# Look inside the recorded database. 0 = NOMINAL, 1 = DEGRADED, 2 = FAULT.
sqlite3 telemetry.db "SELECT health_state, COUNT(*) FROM telemetry_frames GROUP BY health_state;"
sqlite3 -header -column telemetry.db "SELECT * FROM telemetry_frames ORDER BY row_id DESC LIMIT 5;"
```

Notice that the daemon runs perfectly well with no viewer. That is a property of UDP, explained in `src/udp_sender.hpp`.

## How the project is organised

```
pi-telemetry-daemon/
├── README.md                  you are here
├── CMakeLists.txt             build description (what to compile, and how)
│
├── src/                       the C++ program
│   ├── main.cpp                 entry point: builds the objects and connects them
│   ├── telemetry_daemon.*       the main loop: read, check health, send, store
│   ├── fixed_rate_timer.*       keeps the loop at exactly 50 Hz without drift
│   ├── health_monitor.*         NOMINAL / DEGRADED / FAULT state machine
│   ├── telemetry_frame.*        the 34-byte message format: encode and decode
│   ├── crc16.*                  the checksum that detects corrupted messages
│   ├── udp_sender.*             sends frames over the network
│   ├── telemetry_store.*        records frames in SQLite
│   ├── daemon_config.*          command line options
│   ├── sensor_reading.hpp       the data a sensor returns
│   ├── sensor_interface.hpp     the contract every sensor follows (the HAL)
│   ├── simulated_sensor.*       fake sensor, used today
│   ├── bme280_sensor.*          real sensor driver, for the Raspberry Pi
│   ├── i2c_bus_interface.hpp    the contract for talking to a chip over I2C
│   └── linux_i2c_bus.*          real I2C access through the Linux kernel
│
├── tests/                     102 C++ unit tests and 9 Python tests
│   ├── test_*.cpp               one test file per source file
│   ├── fake_i2c_bus.hpp         a pretend chip, for testing the driver
│   └── test_end_to_end.py       launches the real daemon and checks its output
│
├── tools/                     the Python ground station
│   ├── telemetry_viewer.py      listens, validates and displays frames
│   └── telemetry_frame.py       Python decoder for the frame format
│
├── scripts/                   one-command helpers
│   ├── setup_ubuntu.sh          install build tools (run once)
│   ├── build_and_test.sh        compile and run every test
│   └── run_demo.sh              run daemon and viewer together
│
├── docs/
│   ├── CODE_WALKTHROUGH.md      what order to read the code in, with exercises
│   ├── DESIGN_DECISIONS.md      each major choice, the options, and why
│   ├── INTERVIEW_NOTES.md       pitch, likely questions, resume wording
│   ├── TROUBLESHOOTING.md       fixes for common setup problems
│   └── RASPBERRY_PI_SETUP.md    steps for when the hardware arrives
│
├── deploy/telemetry-daemon.service        run at boot on a Pi (template)
└── .github/workflows/build-and-test.yml   automatic testing on GitHub
```

Every `.hpp` file starts with a comment block explaining what the file is for, the terms it uses, and why it was designed that way. The matching `.cpp` file holds the implementation.

**`.hpp` and `.cpp`:** a C++ class is normally split in two. The header (`.hpp`) declares what exists: the class name, its functions, and their parameters. The source file (`.cpp`) contains the actual code. Other files `#include` only the header.

## Naming conventions

The same rules are used everywhere, so a name tells you what kind of thing it is:

| Kind of thing | Style | Example |
|---|---|---|
| Classes, structs, enums | `PascalCase` | `HealthMonitor`, `TelemetryFrame` |
| Functions and variables | `snake_case` | `read_sensor()`, `frame_limit` |
| Private member variables | `snake_case_` with a trailing underscore | `next_sequence_number_` |
| Constants | `k` followed by `PascalCase` | `kFrameSizeBytes` |
| Abstract interfaces | name ends in `Interface` | `SensorInterface` |
| Files | `snake_case`, named after the class inside | `health_monitor.hpp` |
| Test files | `test_` followed by the file being tested | `test_health_monitor.cpp` |

Two further rules:

- **Units are part of the name.** `temperature_centi_celsius`, `timestamp_milliseconds`, `pressure_pascals`. Nobody has to guess.
- **No abbreviations.** `consecutive_read_failures`, not `cons_fail_cnt`.

## What is verified and what is not

This is worth being precise about, especially in an interview.

**Verified by running it:**

- The full pipeline with the simulated sensor: timing loop, state machine, frame encoding, UDP, SQLite, and the Python viewer.
- 102 C++ unit tests (GoogleTest) and 9 Python tests (4 of them end-to-end), all passing.
- The same tests passing under AddressSanitizer and UndefinedBehaviorSanitizer, which rule out memory errors and undefined behaviour on every path the tests exercise.
- The CRC against its published check value, and against a second independent implementation in Python.
- The BME280 temperature and pressure formulas against the worked example in Bosch's datasheet.
- The BME280 driver's register sequence, against a fake I2C bus.

**Not yet verified:**

- Anything on real hardware. `src/linux_i2c_bus.cpp` and the BME280 driver have never talked to a physical chip. The datasheet has no worked example for humidity, so that formula is checked only for sensible behaviour.
- `deploy/telemetry-daemon.service` and the GitHub Actions workflow have not been run.
- Hard real-time behaviour. Linux can delay any program by a few milliseconds, so the loop is steady on average but has no guaranteed worst case. The daemon counts and reports any cycle that misses its deadline.

## Where to go next

1. **Understand it:** [docs/CODE_WALKTHROUGH.md](docs/CODE_WALKTHROUGH.md) gives a reading order and three small exercises.
2. **Know why it is built this way:** [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md).
3. **Talk about it:** [docs/INTERVIEW_NOTES.md](docs/INTERVIEW_NOTES.md).
4. **When the Pi arrives:** [docs/RASPBERRY_PI_SETUP.md](docs/RASPBERRY_PI_SETUP.md).
