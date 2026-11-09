# SPDX-License-Identifier: GPL-3.0-or-later
"""Compose honest live-Painter/native-reference comparison artifacts.

Uses saved screenshots and native renderer output, never synthesizes Painter
images. Camera and lighting are not registered; this is a visual review sheet,
not a pixel-equivalence measurement.
"""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps

parser = argparse.ArgumentParser()
parser.add_argument('--reference', type=Path, required=True)
parser.add_argument('--native', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
records = json.loads((args.native / 'mpainter_smart_mask_parameters.json').read_text())
font_path = '/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf'
if not Path(font_path).exists():
    font_path = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
font = ImageFont.truetype(font_path, 17) if Path(font_path).exists() else ImageFont.load_default()
small = ImageFont.truetype(font_path, 13) if Path(font_path).exists() else font
rows = [r for r in records if all((args.reference / f"sp_live_{r['name'].lower().replace(' ', '_')}_{v}.png").exists() for v in ('low', 'default', 'high'))]
width, row_height, header = 1920, 360, 110
sheet = Image.new('RGB', (width, header + row_height * len(rows)), '#1e1e1e')
draw = ImageDraw.Draw(sheet)
draw.text((16, 10), 'Live Substance Painter / mPainter native evaluator — Shader_Ball_JL_01', font=font, fill='white')
draw.text((16, 36), 'Saved Painter slider values replayed. Separate cameras/lighting; visual comparison, pixel equivalence NOT verified.', font=small, fill='#c9c9c9')
for col, state in enumerate(('low', 'default', 'high')):
    draw.text((col * 640 + 12, 65), state.upper() + '       Painter screenshot                         mPainter reference', font=small, fill='#88b6ff')
manifest = []
for row, record in enumerate(rows):
    name = record['name']
    slug = name.lower().replace(' ', '_')
    y = header + row * row_height
    draw.text((12, y), name, font=font, fill='white')
    for col, state in enumerate(('low', 'default', 'high')):
        screenshot = args.reference / f'sp_live_{slug}_{state}.png'
        mask_screenshot = args.reference / f'sp_live_{slug}_{state}_mask.png'
        mode = 'Mask' if mask_screenshot.exists() else 'Material'
        if mask_screenshot.exists():
            screenshot = mask_screenshot
        original = Image.open(screenshot).convert('RGB')
        w, h = original.size
        # Fixed viewport crop; preserve all mesh pixels and exclude the UI.
        crop = original.crop((int(.26*w), int(.067*h), int(.525*w), int(.68*h)))
        native_path = args.native / f"{name}_{state}{'_material' if mode == 'Material' else ''}.png"
        native = Image.open(native_path).convert('RGB')
        for offset, im in ((0, crop), (320, native)):
            thumbnail = ImageOps.contain(im, (300, 290))
            x = col*640 + offset + (320-thumbnail.width)//2
            sheet.paste(thumbnail, (x, y+26+(290-thumbnail.height)//2))
        variant = next(v for v in record['variants'] if v['variant'] == state)
        if not variant['live_evidence']:
            raise RuntimeError(f'Missing saved live parameters: {name} {state}')
        draw.text((col*640+12, y+320), f'{mode} view · exact saved control values · separate renderers', font=small, fill='#bcbcbc')
        manifest.append({'mask': name, 'state': state, 'painter': str(screenshot), 'native': str(native_path), 'view': mode, 'live_parameters_replayed': True})
args.output.parent.mkdir(parents=True, exist_ok=True)
sheet.save(args.output)
args.output.with_suffix('.json').write_text(json.dumps({'camera_registered': False, 'pixel_equivalence_verified': False, 'pairs': manifest}, indent=2)+'\n')
print(f'Wrote {len(manifest)} genuine-reference pairs to {args.output}')
