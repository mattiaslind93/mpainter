#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail a staged mPainter package that is missing required license files.

Read-only. Exit 0 only when the mandatory texts and a few content checks pass.
This is a release gate, not legal advice.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys


def exist(path: pathlib.Path) -> bool:
    return path.is_file()


def find_named(root: pathlib.Path, name: str) -> list[pathlib.Path]:
    return [p for p in root.rglob(name) if p.is_file()]


def find_qt_license(root: pathlib.Path, filename: str) -> bool:
    licenses = root / "licenses" / "Qt"
    if not licenses.exists():
        return False
    return any(p.name == filename for p in licenses.rglob(filename))


def read(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=pathlib.Path, help="Staged package directory")
    parser.add_argument(
        "--expect-commit",
        help="Require this 40-character commit in BUILD-INFO.txt",
    )
    parser.add_argument(
        "--expect-version",
        help="Require this product version in BUILD-INFO.txt or filenames",
    )
    parser.add_argument(
        "--expect-materials",
        type=int,
        help="Require exactly this many files under assets/materials",
    )
    args = parser.parse_args()
    root = args.package.resolve()
    if not root.is_dir():
        print(f"FAIL: package directory does not exist: {root}", file=sys.stderr)
        return 2
    # A macOS bundle keeps its texts in Contents/Resources and Qt in Frameworks/PlugIns.
    contents = root / "Contents" if root.suffix == ".app" else None
    if contents:
        root = contents / "Resources"

    missing: list[str] = []
    notes: list[str] = []

    for name in ("LICENSE", "NOTICE.md", "README.md"):
        if not exist(root / name):
            missing.append(name)

    license_text = read(root / "LICENSE")
    if exist(root / "LICENSE") and "GNU GENERAL PUBLIC LICENSE" not in license_text:
        missing.append("LICENSE does not look like the GNU GPL text")

    build_info = root / "BUILD-INFO.txt"
    if not exist(build_info):
        missing.append("BUILD-INFO.txt")
        info = ""
    else:
        info = read(build_info)
        if args.expect_version and args.expect_version not in info:
            missing.append(f"BUILD-INFO.txt does not name version {args.expect_version}")
        commit_match = re.search(r"\b[0-9a-f]{40}\b", info)
        if not commit_match:
            missing.append("BUILD-INFO.txt has no 40-character git commit")
        elif args.expect_commit and commit_match.group(0) != args.expect_commit:
            missing.append(
                f"BUILD-INFO.txt commit {commit_match.group(0)} != {args.expect_commit}"
            )

    if not find_qt_license(root, "LGPL-3.0-only.txt"):
        missing.append("licenses/Qt/**/LGPL-3.0-only.txt")
    if not find_qt_license(root, "GPL-3.0-only.txt"):
        missing.append("licenses/Qt/**/GPL-3.0-only.txt")

    shaderball = find_named(root, "shaderball-jl01.md") + find_named(
        root, "Shader_Ball_JL_01-NOTICE.md"
    )
    if not shaderball:
        missing.append("Shader Ball CC BY notice (licenses/shaderball-jl01.md or assets/meshes)")
    if not find_named(root, "CC-BY-4.0.txt"):
        missing.append("CC-BY-4.0.txt")

    nvidia = []
    for pattern in (
        "libcuda.so*",
        "libnvoptix.so*",
        "libnvidia*",
        "nvcuda.dll",
        "nvoptix.dll",
    ):
        nvidia.extend(root.rglob(pattern))
    nvidia = [p for p in nvidia if p.is_file()]
    if nvidia:
        missing.append(
            "NVIDIA driver library must not be bundled: "
            + ", ".join(str(p.relative_to(root)) for p in nvidia[:8])
        )

    optix_kernels = list(root.rglob("kernel_optix*.ptx.zst"))
    if optix_kernels and not exist(root / "licenses" / "OptiX" / "LICENSE.txt"):
        missing.append("licenses/OptiX/LICENSE.txt (package ships compiled OptiX kernels)")

    materials_dir = root / "assets" / "materials"
    material_count = (
        sum(1 for p in materials_dir.rglob("*") if p.is_file()) if materials_dir.is_dir() else 0
    )
    if args.expect_materials is not None and material_count != args.expect_materials:
        missing.append(
            f"assets/materials has {material_count} files, expected {args.expect_materials}"
        )

    network_libs = list(root.glob("Qt6Network.dll")) + list(root.glob("lib/libQt6Network.so*"))
    tls_plugins = list((root / "tls").glob("*")) + list((root / "plugins" / "tls").glob("*"))
    if contents:
        network_libs += list((contents / "Frameworks").glob("QtNetwork.framework"))
        tls_plugins += list((contents / "PlugIns" / "tls").glob("*"))
    tls_plugins = [p for p in tls_plugins if p.is_file()]
    real_tls = [
        p
        for p in tls_plugins
        if any(name in p.name.lower() for name in ("openssl", "schannel", "securetransport"))
    ]
    if network_libs and not real_tls:
        missing.append(
            "Qt6Network is present but no OpenSSL/Schannel/SecureTransport TLS plugin "
            f"(found: {[p.name for p in tls_plugins] or 'no tls directory'})"
        )

    if (root / "AppRun").exists() and not exist(
        root / "licenses" / "AppImage-runtime-LICENSE"
    ):
        # Portable tarball has AppRun too; the license is only required after
        # package-appimage.py has run. Warn rather than fail the tarball stage.
        notes.append(
            "AppRun is present but licenses/AppImage-runtime-LICENSE is not; "
            "required for the finished AppImage"
        )

    print(f"package: {root}")
    print(f"material files: {material_count}")
    print(f"OptiX kernels: {len(optix_kernels)}")
    print(f"TLS plugins: {[p.name for p in tls_plugins]}")
    for note in notes:
        print(f"NOTE: {note}")
    if missing:
        print("FAIL:")
        for item in missing:
            print(f"  - {item}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
