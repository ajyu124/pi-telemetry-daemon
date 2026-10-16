# Raspberry Pi setup

Steps for moving from the simulated sensor to a real BME280 on a Raspberry Pi.

> **Status: not yet tested on hardware.** Everything in this file was written before the Pi and sensor arrived. The software side is unit tested against a fake chip and against the numbers in Bosch's datasheet, but no step below has been run on a real board. Expect to adjust something. When you have done it once, correct this file and delete this notice. That edit is itself a good thing to mention in an interview.

## What you need

- Raspberry Pi (a Pi 4 or a Pi Zero 2 W) with power supply and microSD card.
- BME280 breakout board with a Qwiic / STEMMA QT connector, and the matching cable or Pi adapter, so there is no soldering.
- The project already working on your laptop with the simulated sensor.

Check the board says **BME280**. The BMP280 looks identical but has no humidity sensor, and this driver will refuse it (see "Problems" below).

## Step 1. Prepare the Pi

1. On your laptop, install **Raspberry Pi Imager** and write **Raspberry Pi OS Lite (64-bit)** to the microSD card. In the Imager's settings, set a username and password, enter your Wi-Fi details, and enable SSH. SSH is a way to open a terminal on another computer over the network.
2. Put the card in the Pi, connect the sensor, and power it on. Wait two minutes.
3. From the Ubuntu terminal on your laptop, connect (replace `pi` with the username you chose):
   ```bash
   ssh pi@raspberrypi.local
   ```

## Step 2. Turn on I2C and find the sensor

I2C is off by default on a Pi.

```bash
sudo raspi-config
```

Choose **Interface Options**, then **I2C**, then **Yes**. Exit, then:

```bash
sudo apt-get update
sudo apt-get install -y i2c-tools
i2cdetect -y 1
```

`i2cdetect` prints a grid. You should see `77` or `76` in it. That number is the sensor's I2C address. If the grid is all dashes, the Pi cannot see the sensor: check the cable is fully seated at both ends.

Confirm the chip type. This reads register `0xD0`, the chip ID, and should print `0x60`:

```bash
i2cget -y 1 0x77 0xD0
```

## Step 3. Copy and build the project on the Pi

From the Ubuntu terminal on your **laptop**:

```bash
scp ~/pi-telemetry-daemon.tar.gz pi@raspberrypi.local:~
```

(If you no longer have the archive, make one first with `tar -czf ~/pi-telemetry-daemon.tar.gz -C ~ --exclude=build pi-telemetry-daemon`. Once the project is on GitHub, `git clone` on the Pi is easier.)

Then in the SSH session on the **Pi**:

```bash
tar -xzf ~/pi-telemetry-daemon.tar.gz -C ~
cd ~/pi-telemetry-daemon
bash scripts/setup_ubuntu.sh
bash scripts/build_and_test.sh
```

The setup script works on Raspberry Pi OS because it is based on Debian, like Ubuntu. The build takes a few minutes on a Pi Zero. All tests should pass here too. That alone is worth noting: it means the same code and the same 111 tests pass on both an x86 laptop and an ARM board.

## Step 4. Run with the real sensor

Open a second SSH session to the Pi for the viewer:

```bash
cd ~/pi-telemetry-daemon
python3 tools/telemetry_viewer.py
```

In the first session, start the daemon (use `0x76` if that is what `i2cdetect` showed):

```bash
./build/telemetry_daemon --sensor bme280 --i2c-address 0x77
```

## Step 5. Check it against reality

This is the verification the unit tests could not do. Go through each line.

| Check | Expected |
|---|---|
| Temperature | Close to the room's real temperature. A degree or two high is normal, because the board warms itself. |
| Pressure | Roughly 101,000 Pa at sea level, dropping about 12 Pa for every metre of altitude. Compare with a local weather report. |
| Humidity | A believable indoor value, usually 30 to 60 %. |
| Breathe gently on the sensor | Humidity and temperature rise within a second or two, then fall back. |
| Unplug the sensor cable while running | `DEGRADED`, then `FAULT` within about 100 ms. Frames keep arriving with `fresh=no`. |
| Plug it back in | `NOMINAL` again shortly after, with fresh values. This exercises the driver's automatic re-setup. |
| `missed deadlines` after a few minutes | Small and not climbing steadily. |

If the humidity value is clearly wrong while temperature and pressure are right, the most likely cause is in how `dig_H4` and `dig_H5` are decoded in `load_calibration()` in `src/bme280_sensor.cpp`, since the datasheet gives no worked example to test that part against.

## Step 6. Send to your laptop over Wi-Fi (optional)

The daemon can send to another machine with `--host`. There is one catch on Windows: WSL sits behind its own private network, so packets from the Pi reach Windows but are not passed on to Ubuntu. The simplest way around it is to run the viewer in Windows itself. It uses only standard Python, so it works there unchanged.

1. Install Python for Windows from python.org.
2. Copy the `tools` folder somewhere on Windows.
3. In PowerShell, in that folder: `python telemetry_viewer.py --listen-ip 0.0.0.0`. Allow it through the firewall when Windows asks.
4. Find your laptop's IP address with `ipconfig` (the IPv4 address of the Wi-Fi adapter).
5. On the Pi: `./build/telemetry_daemon --sensor bme280 --host YOUR_LAPTOP_IP`.

Now watch the "frames lost" count in the viewer. Over Wi-Fi it may no longer be zero, and that is the sequence number doing its job.

## Step 7. Start at boot (optional)

`deploy/telemetry-daemon.service` is a systemd unit file. Edit the three marked lines in it, then:

```bash
sudo cp deploy/telemetry-daemon.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now telemetry-daemon
systemctl status telemetry-daemon
```

`sudo systemctl stop telemetry-daemon` sends the daemon a SIGTERM signal, which `src/main.cpp` handles by finishing the current cycle and committing the database before exiting.

## Problems

**`could not open /dev/i2c-1: No such file or directory`.** I2C is not enabled. Repeat step 2 and reboot.

**`could not open /dev/i2c-1: Permission denied`.** Your user is not in the `i2c` group. Run `sudo usermod -aG i2c $USER`, then log out and back in.

**`could not initialize the 'bme280' sensor` with no further detail.** The bus opened but the chip did not identify as a BME280. Run `i2cget -y 1 0x77 0xD0`. If it prints `0x58` the board is a BMP280, which has no humidity sensor. If the command fails, the address is wrong: try `0x76`.

**Steadily climbing missed deadlines.** Ordinary Linux does not guarantee timing. Things to try, in order: close other programs on the Pi, run with `--no-database` to see whether SD card writes are the cause, and run the daemon at a higher priority with `sudo chrt -f 50 ./build/telemetry_daemon ...`, which asks Linux to schedule it ahead of normal programs.

## After it works

Update the resume bullet and `README.md` to say the driver is verified on hardware, and replace this file's status notice with what you actually observed.
