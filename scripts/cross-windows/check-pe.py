#!/usr/bin/env python3
"""Inspect PE imports without running code; unresolved non-OS imports fail.

This is a static check, not proof that delay loads/plugins or a Windows launch work.
"""
import argparse
import json
import re
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('root', type=Path, help='Staged runtime directory')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--allow-optional-gpu-drivers', action='store_true',
               help='Report external OIDN CUDA/HIP backend imports separately; never exempt core imports')
a = p.parse_args()
# Windows 10/11 platform DLLs; VC runtime DLLs deliberately are NOT exempt.
os_dlls = set('''advapi32 authz avrt avicap32 bcrypt cfgmgr32 clbcatq comctl32 comdlg32 crypt32
cryptbase cryptsp d2d1 d3d11 d3d12 d3d9 dbghelp dcomp dhcpcsvc dnsapi dsound
 dwmapi dwrite dxgi dxva2 gdi32 glu32 hid imm32 iphlpapi kernel32 kernelbase
mpr msacm32 msimg32 msvcrt mswsock ncrypt netapi32 normaliz ntdll ole32 oleacc
oleaut32 opengl32 powrprof profapi propsys psapi rasapi32 rpcrt4 secur32 setupapi
shell32 shfolder shcore shlwapi user32 userenv usp10 uxtheme version win32u
windowscodecs winhttp wininet winmm winspool wintrust wldap32 ws2_32 wtsapi32
uiautomationcore wintab32 mf mfplat mfreadwrite mfuuid vfw32 winmmbase ucrtbase pathcch'''.split())
# Tablet support is optional/dynamically provided by the device driver.
os_dlls.discard('wintab32')
files = sorted(f for f in a.root.rglob('*') if f.is_file() and f.suffix.lower() in {'.exe', '.dll', '.pyd'})
if not files:
    raise SystemExit('No PE files found')
index = {}
for f in files:
    index.setdefault(f.name.casefold(), []).append(f)
records = []
missing = []
external_optional = []
# OIDN loads these device modules dynamically. The CPU/core path does not
# import them; their CUDA driver/HIP runtime belongs to the target machine.
optional_gpu = {
    ('openimagedenoise_device_cuda.dll', 'nvcuda.dll'),
    ('openimagedenoise_device_hip.dll', 'amdhip64_7.dll'),
}
for f in files:
    out = subprocess.check_output(['llvm-readobj', '--file-headers', '--coff-imports', str(f)], text=True)
    if 'IMAGE_FILE_MACHINE_AMD64' not in out:
        raise SystemExit(f'Not Windows x64: {f}')
    imports = re.findall(r'^  Name: (.+\.dll)\s*$', out, flags=re.M | re.I)
    resolved = {}
    for name in imports:
        key = name.casefold()
        if key.startswith(('api-ms-win-', 'ext-ms-win-')) or key.removesuffix('.dll') in os_dlls:
            resolved[name] = 'Windows system/API set'
        else:
            # A dependency in an unrelated plugin subdirectory is not on the
            # normal loader search path. Require app-root or importer-local DLL.
            candidates = [x for x in index.get(key, []) if x.parent in {f.parent, a.root}]
            if candidates:
                resolved[name] = str(candidates[0].relative_to(a.root))
            elif a.allow_optional_gpu_drivers and (f.name.casefold(), key) in optional_gpu:
                resolved[name] = 'External optional GPU backend dependency (not bundled or verified)'
                external_optional.append({'file': str(f.relative_to(a.root)), 'import': name})
            else:
                resolved[name] = None
                missing.append({'file': str(f.relative_to(a.root)), 'import': name})
    records.append({'file': str(f.relative_to(a.root)), 'imports': resolved})
a.output.write_text(json.dumps({'files': records, 'unresolved': missing, 'external_optional': external_optional}, indent=2) + '\n')
print(f'{len(files)} x64 PE files; {len(missing)} unresolved required imports; '
      f'{len(external_optional)} external optional GPU backend imports')
raise SystemExit(bool(missing))
