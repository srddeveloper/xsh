#!/usr/bin/env bash
set -euo pipefail

# ── xsh installer ──────────────────────────────────────────
# Fetches the C++ source from GitHub, compiles it, and
# installs the binary to /usr/local/bin/xsh.
# Usage:  curl -fsSL <raw-url>/install.sh | bash
# ───────────────────────────────────────────────────────────

REPO_RAW="https://raw.githubusercontent.com/srddeveloper/xsh/refs/heads/main"
SOURCE_URL="${REPO_RAW}/source"
INSTALL_DIR="/usr/local/bin"
BIN_NAME="xsh"
TMP_DIR="$(mktemp -d)"

cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT

# ── colours ────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
RESET='\033[0m'

info()  { printf "${CYAN}[*]${RESET} %s\n" "$1"; }
ok()    { printf "${GREEN}[✓]${RESET} %s\n" "$1"; }
fail()  { printf "${RED}[✗]${RESET} %s\n" "$1"; exit 1; }

# ── banner ─────────────────────────────────────────────────
echo ""
printf "${CYAN}"
echo "  ════════════════════════════════════════════"
echo "             XSH - Developer Utility CLI      "
echo "  ════════════════════════════════════════════"
printf "${RESET}"
echo ""

# ── preflight ──────────────────────────────────────────────
command -v curl  >/dev/null 2>&1 || fail "curl is required but not installed."

# detect a usable C++ compiler
CXX=""
for candidate in g++ clang++ c++; do
    if command -v "$candidate" >/dev/null 2>&1; then
        CXX="$candidate"
        break
    fi
done
[ -n "$CXX" ] || fail "No C++ compiler found. Install g++ or clang++ first (e.g. xsh install build-essential)."

info "Using compiler: $CXX"

# ── download ───────────────────────────────────────────────
SRC_FILE="${TMP_DIR}/xsh.cpp"
info "Downloading source from GitHub..."
curl -fsSL "$SOURCE_URL" -o "$SRC_FILE" || fail "Failed to download source."
ok "Source downloaded."

# ── compile ────────────────────────────────────────────────
BIN_FILE="${TMP_DIR}/${BIN_NAME}"
info "Compiling..."
$CXX -std=c++17 -O2 -Wall -Wextra -pthread -o "$BIN_FILE" "$SRC_FILE" || fail "Compilation failed."
ok "Compiled successfully."

# ── install ────────────────────────────────────────────────
info "Installing to ${INSTALL_DIR}/${BIN_NAME}..."

install_bin() {
    install -m 755 "$BIN_FILE" "${INSTALL_DIR}/${BIN_NAME}"
}

if [ -w "$INSTALL_DIR" ]; then
    install_bin
elif command -v sudo >/dev/null 2>&1; then
    sudo install -m 755 "$BIN_FILE" "${INSTALL_DIR}/${BIN_NAME}"
else
    fail "Cannot write to ${INSTALL_DIR} and sudo is not available. Run as root or move the binary manually."
fi

ok "Installed ${BIN_NAME} → ${INSTALL_DIR}/${BIN_NAME}"

# ── verify ─────────────────────────────────────────────────
if command -v "$BIN_NAME" >/dev/null 2>&1; then
    echo ""
    ok "Ready. Run 'xsh' to start."
else
    echo ""
    info "Binary installed but ${INSTALL_DIR} may not be in your PATH."
    info "Add it with:  export PATH=\"${INSTALL_DIR}:\$PATH\""
fi
