#!/usr/bin/env python3
"""Pin the observed VS package manifest; xwin itself skips this checksum."""
import hashlib
import json
import os
from pathlib import Path
from urllib.request import urlopen

payload = json.loads(Path(__file__).with_name('msvc-channel.json').read_text())['channelItems'][0]['payloads'][0]
cache = Path(os.environ['MP_WIN_WORK']) / 'cache/windows/xwin/dl'
cache.mkdir(parents=True, exist_ok=True)
target = cache / f"pkg_manifest_{payload['sha256']}.vsman"
# Microsoft's channel hash/URL key differs from the actual returned file hash.
# This is the SHA-256 of the manifest used by the verified local acquisition.
expected = '963760b01f621ed4fa40d1cf97aeb042f7bc6e04e3176c13c38abb19d265f789'
if not target.exists():
    with urlopen(payload['url'], timeout=90) as response:
        target.write_bytes(response.read())
if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
    raise SystemExit('VS package manifest changed; refusing unpinned SDK acquisition')
print('Verified VS package manifest:', expected)
