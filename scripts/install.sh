#!/bin/sh
# cerco installer — https://github.com/agusgarcia3007/cerco
#
#   curl -fsSL https://raw.githubusercontent.com/agusgarcia3007/cerco/main/scripts/install.sh | sh
#
# Env overrides:
#   CERCO_REPO     git repo to clone (default: the GitHub URL above)
#   CERCO_BRANCH   branch to build (default: main)
#   CERCO_DIR      where the source lives (default: ~/.cerco/src)
#   CERCO_SKIP_DEPS=1  do not try to install the toolchain
set -eu

REPO="${CERCO_REPO:-https://github.com/agusgarcia3007/cerco.git}"
BRANCH="${CERCO_BRANCH:-main}"
DEST="${CERCO_DIR:-$HOME/.cerco/src}"
PREFIX_DIR="$HOME/.local/bin"

log() { printf 'cerco: %s\n' "$1"; }
fail() { printf 'cerco: error: %s\n' "$1" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

# ------------------------------------------------------------ package manager

install_deps_macos() {
  have brew || fail "Homebrew is required on macOS: https://brew.sh (then re-run)"
  # Apple clang builds the CLI, but the wasm target needs llvm's clang,
  # wasm-ld (lld) and llvm-ar. The CLI finds them under Homebrew prefixes.
  log "installing llvm + lld via Homebrew (this can take a while)"
  brew install llvm lld
}

install_deps_linux() {
  if have apt-get; then
    SUDO=""
    [ "$(id -u)" = "0" ] || have sudo && SUDO="sudo"
    log "installing clang lld llvm via apt"
    $SUDO apt-get update && $SUDO apt-get install -y clang lld llvm
  elif have dnf; then
    log "installing clang lld llvm via dnf"
    have sudo && sudo dnf install -y clang lld llvm || dnf install -y clang lld llvm
  elif have pacman; then
    log "installing clang lld llvm via pacman"
    have sudo && sudo pacman -S --needed --noconfirm clang lld llvm || fail "re-run as root"
  else
    fail "no supported package manager found; install clang, lld and llvm manually, set CERCO_SKIP_DEPS=1 and re-run"
  fi
}

deps_ok() {
  have clang || return 1
  # wasm-ld and llvm-ar may live in the llvm tree instead of PATH
  if ! have wasm-ld && ! ls /opt/homebrew/opt/llvm/bin/wasm-ld >/dev/null 2>&1 \
     && ! ls /usr/local/opt/llvm/bin/wasm-ld >/dev/null 2>&1; then
    return 1
  fi
  have llvm-ar || have ar || return 1
  return 0
}

if [ "${CERCO_SKIP_DEPS:-0}" != "1" ] && ! deps_ok; then
  case "$(uname -s)" in
    Darwin) install_deps_macos ;;
    Linux) install_deps_linux ;;
    *) fail "unsupported platform: $(uname -s) (windows is not supported)" ;;
  esac
fi

# ------------------------------------------------------------------- checkout

if [ -d "$DEST/.git" ]; then
  log "updating existing checkout at $DEST"
  git -C "$DEST" fetch --depth 1 origin "$BRANCH"
  git -C "$DEST" reset --hard "origin/$BRANCH"
else
  rm -rf "$DEST"
  mkdir -p "$(dirname "$DEST")"
  log "cloning $REPO (branch $BRANCH) into $DEST"
  git clone --depth 1 --branch "$BRANCH" "$REPO" "$DEST" \
    || fail "git clone failed"
fi

# ---------------------------------------------------------------------- build

log "building (this compiles the CLI + vendored libuv/llhttp)"
make -C "$DEST" >/dev/null || fail "build failed; re-run with: make -C $DEST"

log "installing to $PREFIX_DIR/cerco"
make -C "$DEST" install >/dev/null

case ":$PATH:" in
  *":$PREFIX_DIR:"*) ;;
  *)
    log "NOTE: $PREFIX_DIR is not on your PATH"
    log "add this to your shell profile:"
    log "  export PATH=\"$PREFIX_DIR:\$PATH\""
    ;;
esac

log "verifying toolchain"
"$PREFIX_DIR/cerco" doctor || fail "cerco doctor reported problems (run it manually for details)"

log "done. start a project:"
log "  cerco new my-app && cd my-app && cerco dev"
