#!/bin/sh
# Build vendored dependencies (libuv + llhttp) into static libs.
# Usage: scripts/build_deps.sh [output-dir]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build}"
OBJ="$OUT/obj"
LIB="$OUT/lib"
mkdir -p "$OBJ" "$LIB"

CC="${CC:-clang}"
CFLAGS="-O2 -fno-strict-aliasing -fPIC"

# ---- libuv (platform sources selected like upstream CMakeLists) ----
UV_COMMON="fs-poll.c idna.c inet.c random.c strscpy.c strtok.c thread-common.c threadpool.c timer.c uv-common.c uv-data-getter-setters.c version.c"
UV_UNIX="async.c core.c dl.c fs.c getaddrinfo.c getnameinfo.c loop-watcher.c loop.c pipe.c poll.c process.c signal.c stream.c tcp.c thread.c tty.c udp.c"
UV_PLAT=""
UV_DEFS="-D_FILE_OFFSET_BITS=64"
LDADD=""

case "$(uname -s)" in
  Darwin)
    UV_PLAT="proctitle.c bsd-ifaddrs.c kqueue.c random-getentropy.c darwin-proctitle.c darwin.c fsevents.c"
    UV_DEFS="$UV_DEFS -D_DARWIN_UNLIMITED_SELECT=1 -D_DARWIN_USE_64_BIT_INODE=1"
    ;;
  Linux)
    UV_PLAT="proctitle.c linux.c procfs-exepath.c random-getentropy.c random-getrandom.c random-sysctl-linux.c"
    UV_DEFS="$UV_DEFS -D_GNU_SOURCE"
    LDADD="-ldl -lpthread -lrt -lnuma 2>/dev/null"
    ;;
  *)
    echo "unsupported platform: $(uname -s)" >&2
    exit 1
    ;;
esac

UV_RANDOM_DEV="random-devurandom.c"
UV_RAND_SRC=""
[ -f "$ROOT/vendor/libuv/src/unix/$UV_RANDOM_DEV" ] && UV_RAND_SRC="$UV_RANDOM_DEV"

UV_FLAGS="$CFLAGS -I$ROOT/vendor/libuv/include -I$ROOT/vendor/libuv/src $UV_DEFS"
UV_OBJS=""
for f in $UV_COMMON; do
  o="$OBJ/uv_$(basename $f .c).o"
  [ -e "$o" ] || $CC -c $UV_FLAGS -o "$o" "$ROOT/vendor/libuv/src/$f"
  UV_OBJS="$UV_OBJS $o"
done
for f in $UV_UNIX $UV_PLAT $UV_RAND_SRC; do
  o="$OBJ/uv_$(basename $f .c).o"
  [ -e "$o" ] || $CC -c $UV_FLAGS -o "$o" "$ROOT/vendor/libuv/src/unix/$f"
  UV_OBJS="$UV_OBJS $o"
done
rm -f "$LIB/libuv.a"
ar rcs "$LIB/libuv.a" $UV_OBJS

# ---- llhttp (generated sources ship in the release tarball) ----
LLHTTP_FLAGS="$CFLAGS -I$ROOT/vendor/llhttp/include -DLLHTTP_STRICT=0 -Wno-unused-but-set-variable"
LLHTTP_OBJS=""
for f in api.c http.c llhttp.c; do
  o="$OBJ/llhttp_$(basename $f .c).o"
  [ -e "$o" ] || $CC -c $LLHTTP_FLAGS -o "$o" "$ROOT/vendor/llhttp/src/$f"
  LLHTTP_OBJS="$LLHTTP_OBJS $o"
done
rm -f "$LIB/libllhttp.a"
ar rcs "$LIB/libllhttp.a" $LLHTTP_OBJS

echo "deps ok: $LIB/libuv.a $LIB/libllhttp.a"
