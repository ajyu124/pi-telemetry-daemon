#!/usr/bin/env bash
# =============================================================================
# setup_ubuntu.sh
#
# PURPOSE
#   Installs everything needed to build and run this project. Run it once.
#
# WORKS ON
#   Ubuntu (including Ubuntu inside WSL on Windows), Debian, and Raspberry Pi
#   OS. These all use the `apt` package manager.
#
# HOW TO RUN
#   bash scripts/setup_ubuntu.sh
#
#   It will ask for your password, because installing software needs
#   administrator rights (that is what `sudo` means). When you type the
#   password nothing appears on screen. That is normal. Press Enter after it.
#
# WHAT IT INSTALLS
#   build-essential   the C++ compiler (g++) and related tools
#   cmake             the build system (see CMakeLists.txt)
#   git               version control, also used to publish to GitHub
#   libsqlite3-dev    the SQLite database library, for src/telemetry_store.cpp
#   sqlite3           a command line tool to look inside the database file
#   libgtest-dev      GoogleTest, the unit testing framework
#   python3           runs the viewer and the end-to-end test
# =============================================================================

# Stop immediately if any command fails, instead of carrying on in a broken state.
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
    echo "Error: this script needs the 'apt-get' package manager (Ubuntu, Debian or Raspberry Pi OS)."
    echo "See docs/TROUBLESHOOTING.md for other systems."
    exit 1
fi

echo "==> Refreshing the list of available packages"
sudo apt-get update

echo "==> Installing build tools and libraries"
sudo apt-get install -y build-essential cmake git libsqlite3-dev sqlite3 libgtest-dev python3

echo
echo "==> Installed versions"
g++ --version | head -n 1
cmake --version | head -n 1
python3 --version
echo
echo "Setup complete. Next step:  bash scripts/build_and_test.sh"
