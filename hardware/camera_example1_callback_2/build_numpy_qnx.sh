#!/bin/bash

# Cross-compilation script for NumPy on QNX 8.0
# This script builds NumPy from source using the QNX Clang toolchain and Meson
# Run this on a Linux host with QNX SDP installed

set -e

# Configuration - Adjust these paths for your QNX SDP installation
QNX_HOST="${QNX_HOST:-/opt/qnx800/host/linux/x86_64}"
QNX_TARGET="${QNX_TARGET:-/opt/qnx800/target/qnx8/aarch64}"
NUMPY_VERSION="${NUMPY_VERSION:-1.26.4}"
BUILD_DIR="${BUILD_DIR:-$(pwd)/numpy_build}"
INSTALL_PREFIX="${INSTALL_PREFIX:-$QNX_TARGET/usr}"

echo "Building NumPy $NUMPY_VERSION for QNX 8.0"
echo "QNX_HOST: $QNX_HOST"
echo "QNX_TARGET: $QNX_TARGET"
echo "Build dir: $BUILD_DIR"
echo "Install prefix: $INSTALL_PREFIX"

# Install build dependencies on host
echo "Installing build dependencies (meson, ninja)..."
pip3 install --user meson ninja

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Download NumPy source if not already present
if [ ! -f "numpy-$NUMPY_VERSION.tar.gz" ]; then
    echo "Downloading NumPy $NUMPY_VERSION..."
    wget "https://github.com/numpy/numpy/releases/download/v$NUMPY_VERSION/numpy-$NUMPY_VERSION.tar.gz"
fi

# Extract source
if [ ! -d "numpy-$NUMPY_VERSION" ]; then
    echo "Extracting NumPy source..."
    tar -xzf "numpy-$NUMPY_VERSION.tar.gz"
fi

cd "numpy-$NUMPY_VERSION"

# Create Meson cross file for QNX
cat > cross_qnx.ini << EOF
[binaries]
c = '$QNX_HOST/usr/bin/qcc'
cpp = '$QNX_HOST/usr/bin/q++'
ar = '$QNX_HOST/usr/bin/ntoarmv7-ar'
strip = '$QNX_HOST/usr/bin/ntoarmv7-strip'
pkgconfig = 'pkg-config'

[host_machine]
system = 'qnx'
cpu_family = 'arm'
cpu = 'armv7'
endian = 'little'

[properties]
c_args = ['-Vgcc_ntoaarch64le', '-D_QNX_SOURCE', '-D_XOPEN_SOURCE=700']
cpp_args = ['-Vgcc_ntoaarch64le', '-D_QNX_SOURCE', '-D_XOPEN_SOURCE=700']
c_link_args = ['-Vgcc_ntoaarch64le']
cpp_link_args = ['-Vgcc_ntoaarch64le']
EOF

# Set up environment
export PATH="$QNX_HOST/usr/bin:$PATH"
export PKG_CONFIG_PATH="$QNX_TARGET/usr/lib/pkgconfig"

# Configure with Meson
echo "Configuring NumPy build with Meson..."
meson setup build --cross-file cross_qnx.ini --prefix "$INSTALL_PREFIX" -Dblas=disabled -Dlapack=disabled

# Build
echo "Building NumPy..."
meson compile -C build

# Install
echo "Installing NumPy..."
meson install -C build

echo "NumPy build complete!"
echo "The NumPy package is installed to $INSTALL_PREFIX/lib/python3.x/site-packages/"
echo "Copy this to your QNX target's Python site-packages directory."
echo ""
echo "To test on QNX target:"
echo "python3 -c \"import numpy as np; print('NumPy version:', np.__version__); print(np.array([1,2,3]) + 5)\""