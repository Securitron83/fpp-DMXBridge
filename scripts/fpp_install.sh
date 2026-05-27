#!/bin/bash
# fpp-DMXBridge install script.
# Called by FPP when the plugin is installed or updated.
# Compiles the C++ plugin .so against the installed FPP source.

set -e

PLUGIN_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PLUGIN_NAME="fpp-DMXBridge"
SONAME="lib${PLUGIN_NAME}.so"

# Locate FPP source (prefer /opt/fpp/src, fall back to $FPPDIR/src)
if [ -d /opt/fpp/src ]; then
    FPP_SRC=/opt/fpp/src
elif [ -n "$FPPDIR" ] && [ -d "$FPPDIR/src" ]; then
    FPP_SRC="$FPPDIR/src"
else
    echo "ERROR: Cannot find FPP source directory. Set FPPDIR or install FPP to /opt/fpp."
    exit 1
fi

echo "fpp-DMXBridge: Installing build dependencies..."
apt-get install -y --no-install-recommends \
    g++ \
    make \
    libjsoncpp-dev

echo "fpp-DMXBridge: Building C++ plugin (FPP_SRC=${FPP_SRC})..."
cd "${PLUGIN_DIR}"
make FPP_SRC="${FPP_SRC}" clean
make FPP_SRC="${FPP_SRC}" -j"$(nproc)"

echo "fpp-DMXBridge: Install complete."
echo "  Restart fppd (FPP Status page) for the new commands to appear."
echo "  Configure and test channels: FPP web UI -> Content Setup -> DMX Bridge."
