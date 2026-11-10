#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Fetch an HTTPS URL with QNetworkAccessManager against a chosen Qt library and
# plugin layout, and log which QtNetwork and TLS backend were actually used.
#
#   probe-packaged-https.sh --probe BIN --lib DIR [--lib DIR...] --plugins DIR [--plugins DIR...]
#                           --log FILE [--url URL]
#
# --lib and --plugins are searched in the order given. The probe binary must use
# RUNPATH (not RPATH) so that LD_LIBRARY_PATH takes precedence.
set -euo pipefail
export LC_ALL=C
probe= log= url=https://download.qt.io/ libs=() plugins=()
while (($#)); do
  case "$1" in
    --probe) probe=$2; shift 2 ;;
    --lib) libs+=("$2"); shift 2 ;;
    --plugins) plugins+=("$2"); shift 2 ;;
    --log) log=$2; shift 2 ;;
    --url) url=$2; shift 2 ;;
    *) echo "unknown argument $1" >&2; exit 2 ;;
  esac
done
[[ -x "$probe" && -n "$log" && ${#libs[@]} -gt 0 && ${#plugins[@]} -gt 0 ]] ||
  { sed -n '4,10p' "$0" >&2; exit 2; }
if readelf -d "$probe" | grep -q '(RPATH)'; then
  echo "$probe uses RPATH, which overrides LD_LIBRARY_PATH; relink it with RUNPATH" >&2; exit 2
fi
ld_path=$(IFS=:; echo "${libs[*]}")
plugin_path=$(IFS=:; echo "${plugins[*]}")
exec > >(tee "$log") 2>&1
echo "date: $(date -Is)"
echo "host: $(uname -n) container: $([[ -e /run/.containerenv ]] && echo yes || echo no)"
echo "system OpenSSL: $(rpm -q openssl-libs 2>/dev/null || echo unknown)"
echo "LD_LIBRARY_PATH=$ld_path"
echo "QT_PLUGIN_PATH=$plugin_path"
resolved=$(LD_LIBRARY_PATH=$ld_path ldd "$probe")
echo "$resolved" | grep -E 'libQt6|ssl|crypto'
network=$(awk '$1 == "libQt6Network.so.6" {print $3}' <<<"$resolved")
echo "QtNetwork: $(readlink -f "$network") sha256 $(sha256sum < "$(readlink -f "$network")" | cut -d' ' -f1)"
for dir in "${plugins[@]}"; do
  for plugin in "$dir"/tls/*.so; do
    [[ -e "$plugin" ]] && echo "TLS plugin: $plugin sha256 $(sha256sum < "$plugin" | cut -d' ' -f1)"
  done
done
echo '--- probe'
set +e
LD_LIBRARY_PATH=$ld_path QT_PLUGIN_PATH=$plugin_path QT_LOGGING_RULES='qt.network.ssl*=true;qt.tlsbackend*=true' \
  "$probe" "$url"
rc=$?
set -e
echo "--- probe exit $rc"
exit "$rc"
