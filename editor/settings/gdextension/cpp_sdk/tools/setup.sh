#!/bin/sh
set -eu
version=3.1.1
case "$(uname -s):$(uname -m)" in
    Darwin:arm64) asset=xmake-bundle-v3.1.1.macos.arm64; sha=a614dedc4dd765209949350ce4a0449638542df8147837fb1ba1ce4e9a0ff8ac ;;
    Darwin:x86_64) asset=xmake-bundle-v3.1.1.macos.x86_64; sha=ca1d0dbda4d58d77ae10a2f6d8641892e98b193ef227b461a804a2409fe2fe4b ;;
    Linux:x86_64) asset=xmake-bundle-v3.1.1.linux.x86_64; sha=fe1da8861a5ee5845d4f6c84e499f6f9c6bf5b5a69c9b7b130930ee28355a6d5 ;;
    *) echo 'No locked xmake 3.1.1 bundle for this host profile. Host/device qualification is required.' >&2; exit 1 ;;
esac
digest() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}';
    else shasum -a 256 "$1" | awk '{print $1}'; fi
}
tool="$HOME/.local/bin/xmake"
mkdir -p "$HOME/.local/bin"
if [ ! -f "$tool" ] || [ "$(digest "$tool")" != "$sha" ]; then
    temporary=$(mktemp "$HOME/.local/bin/xmake-download.XXXXXX")
    trap 'rm -f "$temporary"' EXIT HUP INT TERM
    curl --fail --location --proto '=https' --tlsv1.2 "https://github.com/xmake-io/xmake/releases/download/v$version/$asset" --output "$temporary"
    [ "$(digest "$temporary")" = "$sha" ] || { echo 'xmake bundle digest mismatch.' >&2; exit 1; }
    chmod 755 "$temporary"
    mv "$temporary" "$tool"
    trap - EXIT HUP INT TERM
fi
if [ "$(uname -s)" = Darwin ]; then
    if ! xcode-select -p >/dev/null 2>&1; then
        xcode-select --install
        echo 'Complete the Apple Command Line Tools installer, then run Check Toolchain.'
        exit 1
    fi
elif command -v c++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1; then
    :
elif command -v pkexec >/dev/null 2>&1; then
    if command -v apt-get >/dev/null 2>&1; then
        pkexec apt-get install -y clang g++
    elif command -v dnf >/dev/null 2>&1; then
        pkexec dnf install -y clang gcc-c++
    elif command -v pacman >/dev/null 2>&1; then
        pkexec pacman -S --needed --noconfirm clang gcc
    elif command -v zypper >/dev/null 2>&1; then
        pkexec zypper --non-interactive install clang gcc-c++
    else
        echo 'Unsupported package manager. Install a C++17 compiler, then run Check Toolchain.' >&2
        exit 1
    fi
else
    echo 'Install a C++17 compiler, then run Check Toolchain. Graphical installation requires polkit/pkexec.' >&2
    exit 1
fi
echo 'Pinned xmake 3.1.1 bundle verified. Check Toolchain compiles and links the C++17 consumer probe; export qualification remains separate.'
