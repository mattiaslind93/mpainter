#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Rebuild only QtNetwork and its TLS plugins from the pinned, patched qtbase 6.8.3
# with OpenSSL loaded at runtime, and stage them in a private overlay next to the
# toolchain Qt. Run inside distrobox bts-build, which built the toolchain Qt.
#
#   rebuild-qt-network-ssl.sh start     configure, then build and stage detached
#   rebuild-qt-network-ssl.sh configure
#   rebuild-qt-network-ssl.sh build     build and stage in the foreground
#   rebuild-qt-network-ssl.sh overlay   stage and verify an existing build
#   rebuild-qt-network-ssl.sh verify    verify the staged overlay
#
# Nothing is ever installed into toolchains/qt or ~/bts/qt: other builds link
# against those prefixes while this runs.
set -euo pipefail
export LC_ALL=C
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
# The physical work disk path contains backslashes, which CMake rewrites; use the
# same simple bind mount as scripts/cross-windows/env.sh.
work=${MP_WORK:-/mnt/mpainter-phase1}
version=6.8.3
# The bts-build container's HOME is $BTS_ROOT/home, so do not derive this from HOME.
bts=${BTS_ROOT:-/home/$(id -un)/bts}
archive=${MP_QT_ARCHIVE:-$bts/downloads/qtbase-everywhere-src-$version.tar.xz}
archive_sha256=56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80
source=$work/deps/qtbase-everywhere-src-$version
build=$work/deps/qt-$version-network-ssl-build
overlay=$work/deps/qt-$version-ssl-overlay
toolchain=$work/toolchains/qt
logs=${MP_LOG_DIR:-$work/runs/2026-11-10-platforms}
jobs=${MP_QT_JOBS:-2}
targets=(Network QTlsBackendOpenSSLPlugin QTlsBackendCertOnlyPlugin)

die() { echo "ERROR: $*" >&2; exit 1; }
[[ -e /run/.containerenv ]] || die 'Run inside distrobox bts-build.'
[[ "$work" == /* && "$work" != *\\* && "$work" != *' '* && -d "$work/toolchains/qt" ]] ||
  die "MP_WORK=$work must be an absolute path without backslashes or spaces that holds toolchains/qt"
for prefix in "$toolchain" "$bts/qt/$version"; do
  for path in "$overlay" "$build"; do
    case "$(readlink -m "$path")/" in "$(readlink -m "$prefix")/"*) die "$path must not be inside $prefix";; esac
  done
done
# Share the BTS compiler cache through the environment only; its config is not changed.
if [[ -d "$bts/build/ccache" ]]; then export CCACHE_DIR="$bts/build/ccache" CCACHE_MAXSIZE=30G; fi

configure() {
  printf '%s  %s\n' "$archive_sha256" "$archive" | sha256sum --check
  [[ -f "$source/CMakeLists.txt" ]] || die "Extract $archive to $source first"
  [[ -f /usr/include/openssl/ssl.h ]] || die 'openssl-devel headers are missing'
  pkg-config --exists openssl || die 'pkg-config cannot find openssl'
  python3 "$here/apply-qt-patches.py" --source "$source"
  cmake -S "$source" -B "$build" -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$work/deps/qt-$version-ssl" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DQT_BUILD_EXAMPLES=OFF \
    -DQT_BUILD_TESTS=OFF -DFEATURE_xcb=ON -DFEATURE_opengl_desktop=ON \
    -DFEATURE_fontconfig=ON \
    -DFEATURE_ssl=ON -DFEATURE_openssl=ON -DFEATURE_openssl_runtime=ON \
    -DFEATURE_openssl_linked=OFF
  local available
  available=$(ninja -C "$build" -t targets all | cut -d: -f1)
  for target in "${targets[@]}"; do
    grep -qx "$target" <<<"$available" || die "No ninja target $target in $build"
  done
  check_config
}

header() {
  local found
  found=$(find "$1" -name "$2" -not -path '*/CMakeFiles/*')
  [[ $(wc -l <<<"$found") == 1 && -n "$found" ]] || die "Expected one $2 under $1, found: ${found:-none}"
  echo "$found"
}

# QtCore's own feature headers must match the toolchain Core it will run against.
# The global qconfig headers and Network may differ only in OpenSSL/SSL features;
# Core reads those only through openssl_hash, which stays off without openssl_linked.
check_config() {
  local bad=0 name delta
  for name in qtcore-config.h qtcore-config_p.h; do
    diff -u "$(header "$toolchain/include" "$name")" "$(header "$build/src" "$name")" ||
      { echo "MISMATCH: $name differs from the toolchain Qt"; bad=1; }
  done
  for name in qconfig.h qconfig_p.h qtnetwork-config.h qtnetwork-config_p.h; do
    delta=$(diff "$(header "$toolchain/include" "$name")" "$(header "$build/src" "$name")" |
      grep '^[<>]' | grep -viE 'ssl|dtls|ocsp' || true)
    if [[ -n "$delta" ]]; then echo "MISMATCH: $name differs outside SSL:"; echo "$delta"; bad=1; fi
  done
  local want
  for want in 'qtnetwork-config.h:QT_FEATURE_ssl 1' 'qconfig_p.h:QT_FEATURE_openssl 1' \
              'qconfig.h:QT_FEATURE_openssl_linked -1' 'qtcore-config_p.h:QT_FEATURE_openssl_hash -1'; do
    grep -qx "#define ${want#*:}" "$(header "$build/src" "${want%%:*}")" ||
      { echo "MISMATCH: ${want%%:*} lacks #define ${want#*:}"; bad=1; }
  done
  ((bad == 0)) || die 'Configured features do not match the toolchain Qt'
  echo 'Config check: QtCore features identical to toolchain; only OpenSSL/SSL features differ (openssl on, loaded at runtime, not linked)'
}

build_targets() {
  cmake --build "$build" --parallel "$jobs" --target "${targets[@]}"
}

stage_overlay() {
  local next=$overlay.next
  rm -rf "$next"; mkdir -p "$next/lib" "$next/plugins/tls"
  cp "$build/lib/libQt6Network.so.$version" "$next/lib/"
  ln -s "libQt6Network.so.$version" "$next/lib/libQt6Network.so.6"
  ln -s libQt6Network.so.6 "$next/lib/libQt6Network.so"
  cp "$build/plugins/tls/libqopensslbackend.so" "$build/plugins/tls/libqcertonlybackend.so" \
    "$next/plugins/tls/"
  patchelf --set-rpath '$ORIGIN' "$next/lib/libQt6Network.so.$version"
  patchelf --set-rpath '$ORIGIN/../../lib' "$next"/plugins/tls/*.so
  {
    echo "QtNetwork $version with OpenSSL loaded at runtime (openssl_runtime ON, openssl_linked OFF)."
    echo "Source: $archive sha256 $archive_sha256"
    echo 'Patches: scripts/host/patches/qt-6.8.3-file-dialog-settings.patch (QtWidgets only)'
    echo "Compiler: $(c++ --version | head -n1)"
    echo "OpenSSL headers: openssl-devel $(rpm -q --qf '%{VERSION}-%{RELEASE}' openssl-devel)"
    echo "Built: $(date -Is) in $build"
    echo 'libssl and libcrypto are not included; the plugin loads the system OpenSSL 3 at runtime.'
    (cd "$next" && find . -type f -name '*.so*' -exec sha256sum {} + | sort -k2)
  } > "$next/MANIFEST.txt"
  rm -rf "$overlay"; mv "$next" "$overlay"
}

defined() { nm -D --defined-only --with-symbol-versions "$@" | awk '{print $NF}' | sed 's/@@/@/' | sort -u; }
undefined_qt() { nm -D --undefined-only --with-symbol-versions "$1" | awk '{print $NF}' | grep '@Qt_' | sort -u; }

verify() {
  local network=$overlay/lib/libQt6Network.so.$version
  local old=$toolchain/lib/libQt6Network.so.$version core=$toolchain/lib/libQt6Core.so.$version
  local bad=0 lines
  echo "== $network"
  readelf -d "$network" | grep -E 'NEEDED|SONAME|RPATH|RUNPATH'
  readelf -d "$network" | grep -q 'SONAME.*\[libQt6Network.so.6\]' || { echo 'FAIL: SONAME'; bad=1; }
  readelf -d "$network" | grep -q 'NEEDED.*\[libQt6Core.so.6\]' || { echo 'FAIL: no NEEDED libQt6Core.so.6'; bad=1; }
  for elf in "$network" "$overlay"/plugins/tls/*.so; do
    if readelf -d "$elf" | grep -E 'NEEDED.*\[lib(ssl|crypto)\.so'; then
      echo "FAIL: $elf links OpenSSL directly"; bad=1
    fi
  done
  echo "NEEDED compared with the toolchain Network:"
  diff <(readelf -d "$old" | grep NEEDED) <(readelf -d "$network" | grep NEEDED) && echo '  identical'
  lines=$(nm -D --defined-only "$network" | grep -c QSslSocket || true)
  echo "QSslSocket exports: $lines (toolchain Network: $(nm -D --defined-only "$old" | grep -c QSslSocket || true))"
  ((lines > 0)) || { echo 'FAIL: no QSslSocket symbols'; bad=1; }
  lines=$(comm -23 <(defined "$old") <(defined "$network"))
  if [[ -n "$lines" ]]; then
    echo "FAIL: symbols exported by the toolchain Network are missing:"; echo "$lines" | head -n 40; bad=1
  else
    echo 'Exports: superset of the toolchain Network'
  fi
  lines=$(comm -23 <(undefined_qt "$network") <(defined "$core"))
  [[ -z "$lines" ]] && echo 'Network Qt imports: all defined by the toolchain Core' ||
    { echo 'FAIL: Network imports missing from toolchain Core:'; echo "$lines"; bad=1; }
  for plugin in "$overlay"/plugins/tls/*.so; do
    lines=$(comm -23 <(undefined_qt "$plugin") <(defined "$core" "$network"))
    [[ -z "$lines" ]] && echo "$(basename "$plugin") Qt imports: all defined by toolchain Core + overlay Network" ||
      { echo "FAIL: $(basename "$plugin") imports missing:"; echo "$lines"; bad=1; }
  done
  local resolved
  resolved=$(LD_LIBRARY_PATH="$overlay/lib:$toolchain/lib" ldd "$overlay/plugins/tls/libqopensslbackend.so")
  echo "$resolved" | grep -E 'libQt6|ssl|crypto'
  for name in libQt6Core.so.6 libQt6Network.so.6; do
    local path want
    path=$(awk -v n="$name" '$1 == n {print $3}' <<<"$resolved")
    [[ $name == libQt6Core.so.6 ]] && want=$toolchain/lib || want=$overlay/lib
    [[ -n "$path" && "$(readlink -f "$(dirname "$path")")" == "$(readlink -f "$want")" ]] ||
      { echo "FAIL: $name resolved to '$path', expected $want"; bad=1; }
  done
  cat "$overlay/MANIFEST.txt"
  ((bad == 0)) || die 'Overlay verification failed'
  echo 'VERIFY PASS'
}

case "${1:-}" in
  configure) configure ;;
  build) build_targets; stage_overlay; verify ;;
  overlay) stage_overlay; verify ;;
  verify) verify ;;
  start)
    configure
    log=$logs/qt-network-ssl-build.log
    setsid nohup bash "${BASH_SOURCE[0]}" build > "$log" 2>&1 < /dev/null &
    echo $! > "$logs/qt-network-ssl-build.pid"
    echo "Build started detached, pid $!, log $log"
    ;;
  wait)
    pid=${2:-$(cat "$logs/qt-network-ssl-build.pid")}
    while kill -0 "$pid" 2>/dev/null; do sleep 30; done
    tail -n 60 "$logs/qt-network-ssl-build.log"
    ;;
  *) die "usage: $0 start|configure|build|overlay|verify|wait [pid]" ;;
esac
