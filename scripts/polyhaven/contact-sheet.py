#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Show the actual embedded JL01 previews, not loose authoring render files."""
import argparse
import base64
import io
import json
import struct
from pathlib import Path
from PIL import Image, ImageDraw

p = argparse.ArgumentParser()
p.add_argument('--library', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
rows = sorted(json.loads((a.library / 'manifest.json').read_text()), key=lambda r: r['name'])
sheet = Image.new('RGB', (6 * 256, ((len(rows) + 5) // 6) * 284), '#242424')
draw = ImageDraw.Draw(sheet)
previews = set()
for index, row in enumerate(rows):
    data = (a.library / row['native_archive']).read_bytes()
    assert data[:8] == b'MFPNAT1\n'
    descriptor = json.loads(data[24:24 + struct.unpack('>Q', data[8:16])[0]])
    preview = base64.b64decode(descriptor['preview_png'])
    assert preview not in previews, ('Duplicate preview', row['name'])
    previews.add(preview)
    image = Image.open(io.BytesIO(preview)).convert('RGBA')
    assert image.size == (256, 256)
    x, y = (index % 6) * 256, (index // 6) * 284
    sheet.paste(image, (x, y), image)
    draw.text((x + 12, y + 262), row['name'], fill='white')
a.output.parent.mkdir(parents=True, exist_ok=True)
sheet.save(a.output, quality=94)
print(f'{len(rows)} unique embedded JL01 previews: {a.output}')
