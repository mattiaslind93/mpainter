# SPDX-License-Identifier: GPL-3.0-or-later
"""No dense CPU map masks in the interactive smart-material application path."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts/host'))
from main_source import compact, main_source, statement, with_sections
source=(root/'native/host/assets/SmartMaterial.cc').read_text()
for forbidden in ('std::vector<float>', 'assign_channel(', 'sample_id_map(', 'sample_resampled('):
    assert forbidden not in source, ('CPU smart-material mask evaluation',forbidden)
# The bundled worn-metal recipe was deliberately removed. User-owned preset
# capture still must use GPU readback and asynchronous publication below.
assert compact('bool worn_metal_ready(const TextureSet &){return false;}') in compact(source)
assert 'Import a user-owned Material Preset before applying it' in source
thumbnail=(root/'native/host/blender/AssetThumbnail.cc').read_text()
assert 'worn_metal_color(' not in thumbnail, 'Smart-material thumbnails may not evaluate a CPU layered stack'
capture=(root/'native/host/assets/MaterialPresetReadback.hh').read_text()
assert 'capture_project_source(' in capture and 'GpuPersistenceScope' in capture and 'write_material_preset(preset_)' in capture
# The whole callback statement, whitespace-free, however the session is formatted.
callback=compact(statement(main_source(root),'callbacks.create_smart_material'))
assert 'host.begin_material_preset(' in callback and 'host.poll_material_preset()' in callback
assert 'AssetCatalog::prepare_user(' in callback and 'assets.add_prepared(' in callback
for forbidden in ('write_material_preset(', 'read_material_preset(', 'assets.add('):assert forbidden not in callback, ('UI material-preset work',forbidden)
inspection=with_sections(root,'native/host/blender/MaterialInspection.cc')
baked=inspection.split('void baked_image(',1)[1].split('\n}',1)[0]
assert 'native_generated_image(&main,resolution,height,' in compact(baked),'Baked-map inspection must allocate the baked map dimensions'
print('Material presets: bundled recipe disabled; GPU capture, async publication, no CPU layered thumbnails')
