# Troubleshooting

Fixes for the problems most likely to come up, in the order you would meet them. Find the message you are seeing and follow the fix.

## Installing WSL

### `wsl --install` prints help text and installs nothing

WSL is already installed, but no Linux is. In PowerShell run:

```
wsl --install -d Ubuntu
```

### Error mentioning "virtualization", `0x80370102`, or "Virtual Machine Platform"

WSL needs a processor feature called virtualization, which is switched off on some computers.

1. Open Task Manager (`Ctrl+Shift+Esc`), go to **Performance**, click **CPU**, and look for **Virtualization** at the bottom right.
2. If it says **Disabled**, it has to be turned on in the computer's BIOS/UEFI settings. Search for your computer's model plus "enable virtualization". The setting is usually called Intel VT-x, Intel Virtualization Technology, or AMD SVM.
3. If it says **Enabled** and WSL still fails, run this in an administrator PowerShell and restart:
   ```
   dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
   ```

### WSL will not install at all: use GitHub Codespaces instead

Codespaces gives you a Linux terminal in the browser with nothing to install. A personal GitHub account includes a free monthly allowance.

1. On github.com, create a new empty repository named `pi-telemetry-daemon`.
2. Unpack `pi-telemetry-daemon.tar.gz` on Windows (right-click, **Extract All**. If Windows cannot open it, install the free tool 7-Zip).
3. On the repository page choose **uploading an existing file**, drag in everything from inside the unpacked folder, and commit.
4. Click the green **Code** button, then the **Codespaces** tab, then **Create codespace on main**.
5. In the terminal at the bottom of the page, run the same three commands as in the README:
   ```bash
   bash scripts/setup_ubuntu.sh
   bash scripts/build_and_test.sh
   bash scripts/run_demo.sh
   ```

Note: dragging files into the browser can skip folders whose names start with a dot, such as `.github`. That only affects the automatic testing on GitHub, not the build.

## Getting the project into Linux

### `tar: /mnt/c/Users/*/Downloads/pi-telemetry-daemon.tar.gz: Cannot open: No such file or directory`

The file is not in your Downloads folder, or has a slightly different name (a second download is often saved as `pi-telemetry-daemon (1).tar.gz`). Find it with:

```bash
ls /mnt/c/Users/*/Downloads/ | grep -i telemetry
```

Then use the real name in quotes, for example:

```bash
tar -xzf "/mnt/c/Users/YOUR_WINDOWS_NAME/Downloads/pi-telemetry-daemon (1).tar.gz" -C ~
```

If your Downloads folder is inside OneDrive, look in `/mnt/c/Users/YOUR_WINDOWS_NAME/OneDrive/Downloads/`.

### `bash: scripts/setup_ubuntu.sh: No such file or directory`

You are not in the project folder. Run `cd ~/pi-telemetry-daemon` and try again. `pwd` prints the folder you are currently in.

### `$'\r': command not found`, or `syntax error near unexpected token`

A script has Windows line endings. This happens if the files were unpacked or edited with a Windows tool and then copied across. Fix every script at once:

```bash
sed -i 's/\r$//' scripts/*.sh
```

To avoid it, unpack with the `tar` command from the README, and keep the project in the Linux home folder (`~`), not under `/mnt/c/`.

## Running `setup_ubuntu.sh`

### It asks for a password and nothing appears when I type

That is normal for Linux. Type the password you invented when Ubuntu was first installed and press Enter.

### I forgot my Linux password

In PowerShell run `wsl -u root`, then `passwd YOUR_LINUX_USERNAME`, set a new one, and type `exit`.

### `Unable to locate package` or `Temporary failure resolving`

The package list is stale or there is no internet. Check the connection, then:

```bash
sudo apt-get update
bash scripts/setup_ubuntu.sh
```

If internet works in Windows but not in Ubuntu, close every Ubuntu window, run `wsl --shutdown` in PowerShell, and open Ubuntu again.

## Building

### `cmake: command not found`, or `No CMAKE_CXX_COMPILER could be found`

The build tools are not installed. Run `bash scripts/setup_ubuntu.sh`.

### `Could NOT find SQLite3`

```bash
sudo apt-get install -y libsqlite3-dev
bash scripts/build_and_test.sh --clean
```

### `GoogleTest: not installed, downloading it` followed by a download error

The build is trying to fetch GoogleTest from the internet and cannot. Install the system copy instead:

```bash
sudo apt-get install -y libgtest-dev
bash scripts/build_and_test.sh --clean
```

### `CMake 3.20 or higher is required`

Your Linux is older than Ubuntu 22.04. Check with `lsb_release -a`. The simplest fix is to install a current Ubuntu from PowerShell: `wsl --install -d Ubuntu-24.04`.

### Strange errors after moving or renaming the project folder

CMake remembers the folder it was configured in. Start clean:

```bash
bash scripts/build_and_test.sh --clean
```

### The build is extremely slow

The project is probably under `/mnt/c/`, which is the Windows drive seen from Linux and is slow for this kind of work. Move it into Linux:

```bash
cp -r /mnt/c/path/to/pi-telemetry-daemon ~/
cd ~/pi-telemetry-daemon
bash scripts/build_and_test.sh --clean
```

## Tests

### A `FixedRateTimer` test or `end_to_end_test` fails once, then passes

Those tests measure real time, so a computer that was briefly very busy can fail them. Run the tests again. If the same test fails repeatedly, it is a real failure.

### See the details of one failing test

```bash
# Run only the tests whose name starts with HealthMonitor
./build/unit_tests --gtest_filter='HealthMonitor.*'

# Run the end-to-end test by itself with full output
python3 tests/test_end_to_end.py build/telemetry_daemon
```

## Running

### Viewer: `could not listen on 127.0.0.1:5005 ... Address already in use`

Another viewer is still running, perhaps in another window. Close it, or stop all of them:

```bash
pkill -f telemetry_viewer.py
```

### The viewer shows nothing

- Is the daemon running in another terminal? Its first lines show the destination it is sending to.
- Do the port numbers match? Both default to 5005.
- Are both programs in the same place? A daemon inside WSL cannot reach a viewer started from plain Windows PowerShell using `127.0.0.1`. Run both inside Ubuntu.

### `ModuleNotFoundError: No module named 'telemetry_frame'`

Run the viewer as `python3 tools/telemetry_viewer.py` from the project folder. The file `tools/telemetry_frame.py` must sit next to it.

### `Error: could not initialize the 'bme280' sensor`

You passed `--sensor bme280` on a computer with no sensor. Leave that option off to use the simulated sensor. On a Raspberry Pi, see `RASPBERRY_PI_SETUP.md`.

### `missed deadlines` is not zero

A cycle occasionally finished late. A handful over a run is expected on ordinary Linux, and more likely when the computer is busy. It becomes a problem only if the number climbs steadily. See decision 8 in `DESIGN_DECISIONS.md`.

### The database keeps growing

Every run adds to `telemetry.db`. It is safe to delete (`rm telemetry.db`), or run with `--no-database`.

## Other systems

**macOS.** The code is standard POSIX and should build with Homebrew (`brew install cmake googletest sqlite`), then `bash scripts/build_and_test.sh`. This has not been tested. The BME280 option is disabled automatically because it needs Linux.

**Fedora, Arch and others.** Install the equivalents of the packages listed at the top of `scripts/setup_ubuntu.sh` with your package manager.
