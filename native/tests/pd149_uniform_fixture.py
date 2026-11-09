# SPDX-License-Identifier: GPL-3.0-or-later
"""Own synthetic PD149 three-set fixture; no user source or Blender dependency."""
from pathlib import Path

def write_fixture(output: Path, private_build_root: Path) -> Path:
    output, private_build_root = output.resolve(), private_build_root.resolve()
    if not output.is_relative_to(private_build_root) or output == private_build_root:
        raise ValueError("PD149 fixture must use a private directory under build")
    output.mkdir(parents=True, exist_ok=False)
    vertices, uvs, faces = [], [], []
    # A uses two distinct owners/tiles; B/C are genuine independent material sets.
    for owner, (name, tile, x) in enumerate((("A", 1001, 0), ("A", 1002, 2), ("B", 1001, 4), ("C", 1001, 6))):
        u = (tile - 1001) % 10
        v = (tile - 1001) // 10
        base = 4 * owner + 1
        vertices.extend(f"v {px+x} {py} 0" for px, py in ((0, 0), (1, 0), (1, 1), (0, 1)))
        uvs.extend(f"vt {px+u} {py+v}" for px, py in ((0, 0), (1, 0), (1, 1), (0, 1)))
        faces.extend((f"o Owner{owner}", f"usemtl {name}",
                      f"f {base}/{base} {base+1}/{base+1} {base+2}/{base+2}",
                      f"f {base}/{base} {base+2}/{base+2} {base+3}/{base+3}"))
    (output / "fixture.mtl").write_text("\n".join(f"newmtl {name}\nKd 1 1 1\nKs 0 0 0\nNs 16" for name in ("A", "B", "C")) + "\n")
    path = output / "fixture.obj"
    path.write_text("\n".join(("mtllib fixture.mtl", *vertices, *uvs, *faces)) + "\n")
    return path


def write_map_source(output: Path) -> Path:
    """Own uncompressed 4x4 RGBA8 TGA, bottom-origin, no color-space metadata.

    Actor explicitly chooses linear AP1 for Color; data roles read raw R.
    RGB remains nonzero even where packed alpha is zero.
    """
    import struct
    path = output / "material-source.tga"
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 4, 4, 32, 8)
    data = bytearray()
    for y in range(4):
        for x in range(4):
            r, g, b, alpha = 32 + 32*x, 24 + 40*y, 16 + 16*(x+y), (0, 85, 170, 255)[(x+y)%4]
            data.extend((b, g, r, alpha))
    path.write_bytes(header + data)
    return path
