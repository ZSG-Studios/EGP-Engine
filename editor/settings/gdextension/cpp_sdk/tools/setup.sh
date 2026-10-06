#!/bin/sh
set -eu
if [ "$(uname -s)" = Darwin ]; then
    if ! xcode-select -p >/dev/null 2>&1; then
        xcode-select --install
        echo 'Complete the Apple Command Line Tools installer, then run Check Toolchain.'
        exit 1
    fi
    if ! command -v cmake >/dev/null 2>&1 && [ ! -x /Applications/CMake.app/Contents/bin/cmake ]; then
        if [ -x /opt/homebrew/bin/brew ]; then
            /opt/homebrew/bin/brew install cmake
        elif [ -x /usr/local/bin/brew ]; then
            /usr/local/bin/brew install cmake
        elif command -v brew >/dev/null 2>&1; then
            brew install cmake
        else
            open https://cmake.org/download/
            echo 'Install CMake from the opened official download page, then run Check Toolchain.'
            exit 1
        fi
    fi
elif command -v cmake >/dev/null 2>&1 && { command -v c++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1; }; then
    echo 'CMake and a C++ compiler are installed.'
elif command -v pkexec >/dev/null 2>&1; then
    if command -v apt-get >/dev/null 2>&1; then
        pkexec apt-get install -y cmake build-essential
    elif command -v dnf >/dev/null 2>&1; then
        pkexec dnf install -y cmake gcc-c++ make
    elif command -v pacman >/dev/null 2>&1; then
        pkexec pacman -S --needed --noconfirm cmake base-devel
    elif command -v zypper >/dev/null 2>&1; then
        pkexec zypper --non-interactive install cmake gcc-c++ make
    else
        echo 'Unsupported package manager. Install CMake and a C++17 compiler, then run Check Toolchain.' >&2
        exit 1
    fi
else
    echo 'Install CMake and a C++17 compiler using your package manager, then run Check Toolchain. Graphical installation requires polkit/pkexec.' >&2
    exit 1
fi
