# SPDX-License-Identifier: GPL-3.0-or-later
"""Guard smart-mask UI paths against accidental synchronous CPU rasterization."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'scripts/host'))
from main_source import compact, main_source
# Whitespace-free, so the checks hold however the session is formatted; no needle below has a space.
main = compact(main_source(root))
callback = main.split('callbacks.create_smart_mask=', 1)[1].split('callbacks.create_smart_material=', 1)[0]
assert 'begin_mask_preset(mask)' in callback and 'poll_mask_preset()' in callback
assert 'write_mask_preset(' not in callback, 'Preset compression belongs to the worker'
assert 'std::async(std::launch::async' in callback and 'prepare_user(' in callback and 'add_prepared(' in callback
assert 'assets.add(' not in callback, 'Preset decoding/blob writes belong to the worker'
model = compact((root / 'native/host/TextureDocument.cc').read_text())
apply = model.split('voidTextureDocument::apply_mask_preset', 1)[1].split('TextureDocument::fill_sample', 1)[0]
assert 'MaskEffectKind::Paint' in apply
for forbidden in ('sample_resampled(', 'visit_pages(', '.materialized(', '.assign_page('):
    assert forbidden not in apply, ('Synchronous smart-mask merge rasterization', forbidden)
job = compact((root / 'native/host/MaskPresetReadback.hh').read_text())
assert 'std::async(std::launch::async' in job
assert 'capture_project_source(' in job and 'readback_.poll()' in job
assert 'GpuPersistenceScopeno_cpu_recipes' in job
thumbnail=compact((root/'native/host/AssetThumbnail.cc').read_text())
mask_thumbnail=thumbnail.split('elseif(asset.smart_mask&&!is_spmsk_asset(asset)){',1)[1].split('elseif(asset.projection_weave',1)[0]
assert 'native_missing=true' in mask_thumbnail
assert '.value(' not in mask_thumbnail and 'composite' not in mask_thumbnail.replace('CPUstack','')
assert 'std::async(std::launch::async' in thumbnail and 'native_generator(result.asset' in thumbnail
assert 'smart_mask_thumbnails->request(asset' in main
native=compact((root/'native/host/SmartMaskThumbnailJobs.cc').read_text())
assert 'gpu->renderer.begin_frame()' in native and 'gpu->renderer.update(' in native
assert 'gpu->download.poll()' in native and 'smart_mask_thumbnail_packet_ready(' in native
for forbidden in ('.value(', 'TextureDocument::composite', 'evaluate_channels(', 'composite_user(', 'composite_displacement('):
    assert forbidden not in native, ('Synchronous smart-mask thumbnail evaluation',forbidden)
print('Smart masks: GPU source capture, asynchronous encoding, COW Paint-stage merge; no UI raster loop')
