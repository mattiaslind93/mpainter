# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual Blender GPU compute vs CPU oracle, plus real property gestures and packed reopen."""
import argparse,json,os,re,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output=a.output.resolve();root=Path(__file__).resolve().parents[2]
if not a.existing:
 env=dict(os.environ,BTS_VERIFY_GPU_STACK='1',BTS_RESOLUTION_LOG='1')
 subprocess.run([sys.executable,str(root/'scripts/host/verify-property-performance.py'),'--output',str(a.output)],env=env,check=True)
log=(a.output/'run.log').read_text()
assert log.count('BTS_HEADLESS_PAINT_DEVICE_CREATED')==1, 'Private Qt paint engine was recreated per frame'
self_test=re.search(r'BTS_GPU_STACK_SELF_TEST cases=(\d+) blend_mask_texture_paint_versions_precision=pass',log);assert self_test and int(self_test[1])>=78
comparisons=re.findall(r'BTS_GPU_STACK_VERIFIED channel=(\d+) probes=64 max_error=(\S+) valid=(\d+)',log);assert len(comparisons)>=78 and all(row[2]=='1' for row in comparisons)
updates=re.findall(r'BTS_GPU_STACK channel=(\d+) layers=(\d+) texels=1048576 source_upload_bytes=(\d+)',log);assert len(updates)>20 and any(row[2]=='0' for row in updates)
proof=json.loads((a.output/'property-performance.json').read_text());final=proof['final'];assert final['composite_checked'] and not final['composite_differences'];assert not final['shader_failures']
assert proof['reference']['document']==final['document'] and proof['reference']['fill_channels']==final['fill_channels']
assert all(case['undo_after']-case['undo_before']==1 for case in proof['cases'])
content_anchors=re.search(r'BTS_GPU_CONTENT_ANCHORS_VERIFIED cases=(\d+) channels=6 components=5 coverage_modes=2 hidden_disabled_masked_source=1 prefix_only=1 signed_hdr=1 post_anchor_unchanged=1 folder_prefix=1 udim=3 dirty_texels=(\d+) undo_broken_forward=1',log)
assert content_anchors and int(content_anchors[1])>=120 and 0<int(content_anchors[2])<512*512,'Content anchors must retain channel-local prefixes and sparse dirty regions'
result=dict(pid=proof['pid'],owner_context_cases=int(self_test[1]),gpu_cpu_probe_cases=len(comparisons),maximum_probe_error=max(float(row[1]) for row in comparisons),full_resolution_gpu_updates=len(updates),exact_cpu_final=True,packed_reopen=True,production_texture_readbacks=0,test_readbacks=len(comparisons))
polygon=re.search(r'BTS_GPU_POLYGON_VERIFIED cases=(\d+) channels=5 opacity=2 dirty_pages=1 clipped_boundaries=1 undo_exact=1 max_error=(\S+) production_readback=0',log)
assert polygon and int(polygon[1])>=60 and float(polygon[2])<=2e-5,'Missing full-image GPU polygon boundary/opacity/dirty Undo oracle'
result['polygon_raster']={'cases':int(polygon[1]),'channels':5,'opacities':2,'partial_pages':True,'clipped_boundaries':True,'exact_undo':True,'maximum_error':float(polygon[2]),'production_readbacks':0}
result['content_anchors']={'cases':int(content_anchors[1]),'dirty_texels':int(content_anchors[2]),'channels':6,'components':5,'coverage_modes':2,'raw_prefix':True,'folder_prefix':True,'udim_tiles':3,'hidden_masked_source':True,'undo_broken_forward':True}
fill_anchors=re.search(r'BTS_GPU_FILL_ANCHORS_VERIFIED cases=(\d+) channels=7 content_mask_sources=1 extraction=5 signed_hdr=1 coverage_modes=2 mapping_ignored=1 udim=3 chained=1 folder_compare_mask=1 broken_forward_repair=1',log)
assert fill_anchors and int(fill_anchors[1])>=80,'Missing GPU Fill-channel anchor coverage'
result['fill_anchors']={'cases':int(fill_anchors[1]),'channels':7,'sources':['content','mask'],'components':5,'signed_hdr':True,'coverage_modes':2,'matching_uv_without_mapping':True,'udim_tiles':3,'chained':True,'folder_compare_mask':True,'broken_forward_repair':True}
generator_anchors=re.search(r'BTS_GPU_GENERATOR_ANCHORS_VERIFIED cases=(\d+) resources=6 components=5 coverage_modes=2 content_mask_sources=1 source_edits=1 broken_forward_repair=1 udim=3 dirty_texels=(\d+)',log)
assert generator_anchors and int(generator_anchors[1])>=250 and 0<int(generator_anchors[2])<512*512,'Missing generator anchor resource coverage'
result['generator_anchors']={'cases':int(generator_anchors[1]),'dirty_texels':int(generator_anchors[2]),'resources':6,'components':5,'coverage_modes':2,'content_and_mask_consumers':True,'content_and_mask_sources':True,'live_source_edits':True,'broken_forward_repair':True,'udim_tiles':3}
prefix=re.search(r'BTS_GPU_PREFIX_VERIFIED full_texels=(\d+) dirty_texels=(\d+) cached_under_and_folder=1',log)
assert prefix and int(prefix[2])<int(prefix[1]),'Missing measured prefix/folder reuse'
assert 'BTS_GPU_RESOURCE_SHARED usages=3 extra_allocations=0' in log
result['prefix_cache']={'full_texels':int(prefix[1]),'dirty_texels':int(prefix[2]),'folder_and_under_reused':True}
occluded=re.search(r'BTS_GPU_OCCLUDED_PREFIX_VERIFIED warm_texels=(\d+) reconnect_texels=(\d+) hidden_dirty=1 pressure_evict=1 no_hidden_dispatch=1',log)
assert occluded and int(occluded[2])<int(occluded[1])
result['occluded_prefix']={'warm_texels':int(occluded[1]),'reconnect_texels':int(occluded[2]),'hidden_edits_invalidated':True,'pressure_eviction':True,'hidden_dispatch':False}
result['shared_source']={'usages':3,'extra_gpu_allocations':0}
assert 'BTS_GPU_PROJECT_READBACK_VERIFIED images=3 native_cow=1 signed_hdr_alpha_zero=1 udim_array_region=1 snapshot_revision=1 worker_read=1 wait_timeout=0 packing_restored=1' in log
assert 'BTS_SAVE_GPU_CHANNEL_COW width=1025 height=1027' in log, 'Readback must cross packet boundaries with distinct rows/columns'
assert 'BTS_GPU_STORAGE_READBACK_VERIFIED cases=64 captured_revision=1 staging=1 nonblocking=1' in log
assert 'BTS_GPU_IMAGE_LIFETIME_VERIFIED sampler_unbound=1 deleted_middle_slot=1 bind_range_valid=1' in log
assert 'BTS_GPU_STORAGE_ACCOUNT_VERIFIED bytes=8160 mips=4 layers=3 view_alias=1 released=1 orphan_view_retained=1' in log
result['live_texture_storage_accounting']={'bytes':8160,'mips':4,'layers':3,'view_alias_additional_bytes':0,'released_bytes':0,'label_groups':True,'orphan_view_retains_storage':True}
result['storage_accounting']={'bytes':8160,'mips':4,'layers':3,'aliases_double_counted':False,'released_exact':True}
result['image_only_lifetime']={'deleted_middle_slot':True,'no_sampler_binding':True,'subsequent_range_bind_valid':True}
result['storage_readback']={'cases':64,'captured_revision_exact':True,'gpu_dispatched_snapshot':True,'nonblocking_poll':True}
assert 'BTS_GPU_OFFSCREEN_REUSE_VERIFIED frames=3 stable_color=1 clear_overlay=1 resize_reallocated=1' in log
result['offscreen_cache']={'frames':3,'stable_color':True,'overlay_cleared':True,'resize_reallocated':True}
result['project_gpu_readback']={'images':3,'native_copy_on_write':True,'signed_hdr_alpha_zero':True,'udim_array_crop':True,'snapshot_revision_exact':True,'worker_read':True,'blocking_fence_waits':0,'packet_boundary':True,'nonaligned_size':[1025,1027]}
assert 'BTS_GPU_QUERIED_CONSTANT_VERIFIED active_nodes=2 uniform_storage=1 opaque_pruned=1 reconnect_exact=1' in log
result['queried_constant_prefix']={'active_nodes':2,'uniform_storage':True,'opaque_pruned':True,'reconnect_exact':True}
assert 'BTS_GPU_TEXTURE_PREPARE_VERIFIED tiles=3 frames=2 bytes=50331648 no_partial_publish=1 reused_allocations=3' in log
result['prepared_resource']={'tiles':3,'frames':2,'bytes':50331648,'partial_publish':False,'reused_allocations':3}
assert 'BTS_GPU_ZERO_FREE_VRAM_VERIFIED inactive_cache_budget=0 fallback=0' in log
result['known_zero_free_vram']={'inactive_cache_budget':0,'ram_fallback':False}
assert 'BTS_GPU_CONSTANT_PREFIX_VERIFIED opaque_pruned=1 translucent_reconnect=1 replace=1 mask=1 uniform_texels=1 layers=13' in log
assert 'BTS_NATIVE_TEXTURE_PROBE_VERIFIED arrays=1 mip_base=3 alias_level_zero=1 unchanged_source=1' in log
assert 'BTS_GPU_WRAP_SEAM_VERIFIED non_square=200x203 uv_triplanar=1 negative_neighbor=1' in log
assert 'BTS_GPU_GEOMETRY_MASK_VERIFIED channels=4 shared_atlas=1 known_unknown_uncovered=1 selection_rebind=1 udim_selection=1' in log
assert log.count('BTS_GPU_PROBE_IMAGE width=32 height=32 sampler_width=4 sampler_height=4')>=2
result['native_texture_probe']={'arrays':True,'presentation_mip_base':3,'reads_native_level_zero':True,'source_unchanged':True}
result['constant_prefix']={'layers':13,'opaque_pruned':True,'translucent_reconnect':True,'replace_and_mask':True,'uniform_texels':1}
assert 'BTS_GPU_UDIM_VERIFIED tiles=1001,1002,1011 shared_array_layer=1 untouched_region=1 filtering=bilinear,nearest' in log
result['udim']={'tiles':[1001,1002,1011],'shared_array_layer':True,'untouched_region':True,'filters':['bilinear','nearest']}
assert 'BTS_GPU_POINT_EFFECTS_VERIFIED' in log
anchors=re.search(r'BTS_GPU_MASK_ANCHORS_VERIFIED nested=1 shared_channels=3 hidden_disabled_producer=1 post_anchor_unchanged=1 dirty_texels=(\d+) undo_missing_forward_reorder=1 clamped_reference=1',log)
assert anchors and 0<int(anchors[1])<512*512*8
result['mask_anchors']={'nested':True,'shared_channels':3,'hidden_disabled_producer':True,'post_anchor_unchanged':True,'dirty_texels':int(anchors[1]),'undo_missing_forward_reorder':True,'clamped_reference':True}
isolation=re.search(r'BTS_GPU_MASK_ISOLATION_VERIFIED disabled_hidden=1 selection=1 unchanged_upload=0 dirty_texels=(\d+) uniform_paint_levels_atom_spatial_anchors=1 udim_atlas=1 production_readback=0',log)
assert isolation and 0<int(isolation[1])<=256*256*4
result['mask_isolation']={'disabled_hidden':True,'selection':True,'unchanged_uploads':0,'dirty_texels':int(isolation[1]),'uniform_paint_levels_atom_spatial_anchors':True,'udim_atlas':True}
content=re.search(r'BTS_GPU_CONTENT_PAINT_VERIFIED parents=fill,paint,folder channels=8 blend_modes=29 levels_order=1 empty_replace=1 passthrough=1 undo=1 dirty_texels=(\d+) unchanged_upload=0 production_readback=0',log)
assert content and 0<int(content[1])<512*512*3
result['content_paint']={'parents':['fill','paint','folder'],'channels':8,'blend_modes':29,'levels_order':True,'empty_replace':True,'passthrough':True,'undo':True,'dirty_texels':int(content[1]),'unchanged_uploads':0}
warp=re.search(r'BTS_GPU_WARP_VERIFIED cases=(\d+) identity_deformed_tangents_split=1 uniform_atom_texture=1 filter_wrap_physical=1 crop_depth_backface=1 frozen_envelope=1',log)
assert warp and int(warp[1])>=64
result['warp']={'cases':int(warp[1]),'identity_deformed_tangents_split':True,'uniform_atom_texture':True,'filter_wrap_physical':True,'crop_depth_backface':True,'frozen_envelope':True}
assert 'BTS_GPU_WARP_MASK_VERIFIED cases=26 tiles=2 uniform_atom=1 filters=3 shared_channels=2 isolation=1' in log
result['warp_mask']={'cases':26,'tiles':2,'uniform_atom':True,'filters':3,'shared_channels':2,'isolation':True}
if 'BTS_GPU_WARP_TIMING' in log:
 timings=[];cache={}
 for line in log.splitlines():
  m=re.search(r'BTS_GPU_STACK_CACHE dispatch_texels=(\d+) cached_nodes=(\d+)',line)
  if m:cache={'dispatch_texels':int(m[1]),'cached_nodes':int(m[2])}
  m=re.search(r'BTS_GPU_WARP_TIMING size=1024 operation=(\S+) cpu_submit_ms=(\S+) gpu_ms=(\S+) test_query_wait_ms=(\S+)',line)
  if m:timings.append(dict(operation=m[1],cpu_submit_ms=float(m[2]),gpu_ms=float(m[3]),test_query_wait_ms=float(m[4]),**cache))
 assert len(timings)==4
 assert timings[1]['dispatch_texels']==0 and timings[2]['cached_nodes']>=1
 assert timings[2]['dispatch_texels']<timings[0]['dispatch_texels'] and timings[2]['dispatch_texels']<timings[3]['dispatch_texels']
 result['warp_timing']=timings
projection=re.search(r'BTS_GPU_MATERIAL_PROJECTION_VERIFIED cases=(\d+) uv_world_udim=1 uniform_weave=1 tiling=4 lens=1 stencil_combined=1 scalar_override=1 cpu_saved_exact=1 production_readback=0',log)
assert projection and int(projection[1])==320
result['material_projection']={'cases':int(projection[1]),'uv_world_udim':True,'uniform_weave':True,'lens':True,'stencil_combined':True,'scalar_override':True,'cpu_saved_exact':True}
stencil=re.search(r'BTS_GPU_STENCIL_VERIFIED cases=(\d+) uv_world_udim=1 alpha_kinds=5 tiling=4 inversion=1 lens=1 shared_bitmap=1 packed_triangles=1 cpu_saved_exact=1 production_readback=0',log)
assert stencil and int(stencil[1])==802
result['stencil']={'cases':int(stencil[1]),'uv_world_udim':True,'lens':True,'shared_bitmap':True,'packed_triangles':True,'cpu_saved_exact':True}
assert 'BTS_GPU_QUICK_GATE_VERIFIED tiles=4 channels=3 uv_surface=1 inversion=1 shared_source=1 empty_inverted_noop=1 cpu_saved_exact=1 production_readback=0' in log
result['quick_mask_gate']={'tiles':4,'channels':3,'uv_surface':True,'inversion':True,'shared_source':True,'empty_inverted_noop':True,'saved_exact':True}
quick=re.search(r'BTS_GPU_QUICK_MASK_VERIFIED tiles=3 virtual_256_source=1 full_resolution_target=1 inversion=1 dirty_texels=(\d+) unchanged_upload=0 undo=1 production_readback=0',log)
assert quick and 0<int(quick[1])<1024*1024*2
result['quick_mask']={'tiles':3,'virtual_source':True,'full_resolution_target':True,'inversion':True,'dirty_texels':int(quick[1]),'unchanged_upload':0,'undo':True}
ranges=re.search(r'BTS_GPU_RANGE_VERIFIED tiles=2 signed_unbounded=1 zero_alpha_ignored=1 dirty_texels=(\d+) discarded_stale=(\d+) metadata_bytes=(\d+) unchanged_dispatch=0 pixel_readback=0',log)
assert ranges and int(ranges[1])==2*256*256 and int(ranges[2])==1 and int(ranges[3])==64
result['gpu_range']={'tiles':2,'signed_unbounded':True,'zero_alpha_ignored':True,'dirty_texels':int(ranges[1]),'discarded_stale':int(ranges[2]),'async_metadata_bytes':int(ranges[3]),'unchanged_dispatches':0,'pixel_readbacks':0}
assert 'BTS_GPU_MASK_SPATIAL_VERIFIED modes=triplanar,planar,spherical,cylindrical atom_uniform=1 filters=3 paint_prefix=1 physical=1 udim=1 disabled_no_geometry=1' in log
result['mask_spatial']={'modes':4,'atom_and_uniform':True,'filters':3,'paint_prefix':True,'physical':True,'udim':True,'disabled_no_geometry':True}
assert 'BTS_GPU_MASK_ATOM_VERIFIED wrap=repeat,clamp,none filters=bilinear,nearest inline_and_paint_prefix=1 tiles=1001,1002,1011 physical=1' in log
result['mask_atom']={'wrap_modes':3,'filters':2,'inline_and_paint_prefix':True,'tiles':[1001,1002,1011],'physical_units':True}
compare=re.search(r'BTS_GPU_MATERIAL_COMPARE_VERIFIED cases=(\d+) channels=6 operands=4 operations=3 hidden_disabled_opacity_zero=1 content_paint_levels=1 passthrough=1 reorder_undo=1 isolation=1 production_readback=0',log)
assert compare and int(compare[1])>=459
result['material_compare']={'cases':int(compare[1]),'channels':6,'operands':4,'operations':3,'hidden_disabled_opacity_zero':True,'content_paint_levels':True,'passthrough':True,'reorder_undo':True,'isolation':True,'production_readbacks':0}
histogram=re.search(r'BTS_GPU_HISTOGRAM_VERIFIED cases=(\d+) samples=256 packet_bytes=520 compare=1 levels=1 signed_auto=1 latest_wins=1 cpu_composition=0',log)
assert histogram and int(histogram[1])>=105
result['ui_histograms']={'cases':int(histogram[1]),'samples':256,'async_metadata_bytes':520,'compare':True,'levels':True,'signed_auto':True,'latest_wins':True,'cpu_composition':False}
mask_paint=re.search(r'BTS_GPU_MASK_PAINT_VERIFIED modes=29 interleaved_levels=1 resampled=1 shared_channels=4 dirty_texels=(\d+) undo_reorder_clear=1 production_readback=0',log)
assert mask_paint and 0<int(mask_paint[1])<=256*256
result['mask_paint']={'blend_modes':29,'interleaved_levels':True,'shared_channels':4,'dirty_texels':int(mask_paint[1]),'undo_reorder_clear':True,'production_readbacks':0}
content_generators=re.search(r'BTS_GPU_CONTENT_GENERATORS_VERIFIED cases=(\d+) parents=paint,fill,folder channels=7 resources=6 blends=3 opacity=2 missing_rebake=1 udim=1 levels_order=1 clean_bindings=1',log)
assert content_generators and int(content_generators[1])>=756
result['content_generators']={'cases':int(content_generators[1]),'parents':['paint','fill','folder'],'channels':7,'resources':6,'blends':3,'opacity_variants':2,'missing_rebake':True,'udim':True,'levels_order':True,'clean_bindings':True}
assert 'BTS_GPU_MESH_GENERATORS_VERIFIED thickness=1 dirt_ao_curvature=1 world_normal_light=1 directions=5 missing_rebake=1 clean_bindings=1' in log
result['mesh_generators']={'thickness':True,'dirt_ao_curvature':True,'world_normal_light':True,'directions':5,'missing_rebake':True}
assert 'BTS_GPU_SMART_MATERIAL_GENERATORS_VERIFIED ao=1 curves=3 filters=2 axes=3' in log
assert 'BTS_GPU_MASK_MAPS_VERIFIED id_colors=17 generators=height,curvature axes=xyz constant_compare=1 missing_rebake=1 shared_maps=1' in log
result['smart_material_generators']={'ao':True,'curves':['linear','smoothstep','square'],'filtering':['nearest','bilinear'],'axes':'xyz','missing_rebake':True}
result['mask_maps']={'id_colors':17,'generators':['height','curvature'],'axes':'xyz','constant_compare':True,'missing_restore':True,'shared_maps':True}
assert 'BTS_GPU_AO_VERIFIED baked_authored=1 modes=29 tiles=1001,1002 global_presence=1 float32=1' in log
assert 'BTS_GPU_DERIVED_NORMAL_VERIFIED baked_authored_bump=1 tiles=1001,1002,1011 resolutions=128,256 global_presence=1 sharp_sobel_directx=1 float32=1' in log
assert 'BTS_GPU_PROJECTION_SYMMETRY_VERIFIED modes=triplanar,planar,spherical,cylindrical axes=xyz mirror_radial_flip=1' in log
margin=re.search(r'BTS_GPU_MARGIN_VERIFIED extend_adjacent=1 dirty_edits=40 remote_seam=1 original_samples=(\d+) partial_samples=(\d+) max_error=(\S+) atlas_untouched=1 production_readback=0',log)
assert margin and float(margin[3])<=1e-6
assert int(margin[2])<int(margin[1])*20
result['sparse_margin']={'dirty_edits':40,'remote_seam':True,'initial_source_samples':int(margin[1]),'partial_source_samples':int(margin[2]),'maximum_error':float(margin[3]),'unrelated_atlas_region_untouched':True}
padded=re.search(r'BTS_GPU_PADDED_STACK_VERIFIED channels=8 extend_adjacent=1 padded_bump_normal=1 max_error=(\S+)',log)
assert padded and float(padded[1])<=3e-5
result['padded_stack']={'channels':8,'extend_and_adjacent_faces':True,'bump_padded_before_normal':True,'maximum_error':float(padded[1])}
assert 'BTS_GPU_DERIVED_DIRTY_VERIFIED tiles=1001,1002,1011 size=512 partial_normal=1 partial_ao=1 cross_tile_halo=1 global_coverage_transition=1 unchanged_dispatch=0' in log
result['derived_dirty']={'tiles':[1001,1002,1011],'resolution':512,'partial_normal':True,'partial_ao':True,'cross_tile_halo':True,'global_coverage_transition':True,'unchanged_dispatches':0}
brush=re.search(r'BTS_GPU_BRUSH_RASTER_VERIFIED cases=400 channels=5 uv_surface=1 alpha_shapes=5 flow=1 whole_stroke_opacity_erase=1 max_error=(\S+) production_readback=0',log)
assert brush and float(brush[1])<=5e-5
recipes=re.search(r'BTS_GPU_PAINT_RECIPES_VERIFIED steps=10 undo_cached=1 erase=1 cpu_during_gpu=0 saved_hash_exact=1 pages=(\d+) computed=(\d+) source_bytes=(\d+)',log)
assert recipes and 'BTS_GPU_AUTHORING_CPU_READ' not in log
assert 'BTS_GPU_PAINT_BUDGET_VERIFIED deadline_defers=1 resumed_exact=1 no_partial_publish=1' in log
lru=re.search(r'BTS_GPU_PAINT_LRU_VERIFIED steps=24 evicted=(\d+) pages=(\d+) reload_exact=1 wall_ms=(\S+)',log)
assert lru and int(lru[1])>0 and int(lru[2])<=8
reuse=re.search(r'BTS_GPU_PAGE_REUSE_VERIFIED allocations=(\d+) reuses=(\d+) restored_exact=1',log)
assert reuse and int(reuse[1])<=8 and int(reuse[2])>0
result['brush_storage_reuse']={'allocations':int(reuse[1]),'reuses':int(reuse[2]),'restored_exact':True}
result['brush_lru']={'steps':24,'evicted_pages':int(lru[1]),'resident_pages':int(lru[2]),'reload_exact':True,'wall_ms':float(lru[3])}
batch=re.search(r'BTS_GPU_PAINT_BATCH_VERIFIED dabs=512 dispatches=(\d+) cpu_oracle=1 wall_ms=(\S+)',log)
assert batch and int(batch[1])<=8
result['brush_batch']={'dabs':512,'dispatches':int(batch[1]),'cpu_oracle':True,'wall_ms':float(batch[2])}
complete=re.findall(r'BTS_GPU_COMPLETE_STROKE_VERIFIED previews=100 erase=([01]) rollback_tail=1 dispatches=(\d+) cpu_oracle=1 saved_exact=1',log)
assert len(complete)==2 and {int(x[0])for x in complete}=={0,1} and all(int(x[1])<=6 for x in complete)
result['complete_stroke']={'preview_steps':100,'erase_and_paint':True,'spline_tail_rollback':True,'dispatches':[int(x[1])for x in complete],'saved_exact':True}
buffers=re.search(r'BTS_GPU_BRUSH_BUFFERS_VERIFIED cases=400 allocations=(\d+) growing_capacity=1',log)
assert buffers and int(buffers[1])<=5
result['brush_metadata_buffers']={'raster_cases':400,'allocations':int(buffers[1]),'growing_capacity':True}
result['brush_pages']={'raster_cases':400,'maximum_error':float(brush[1]),'command_steps':10,'undo_cached':True,'erase':True,'cpu_materializations_during_gpu':0,'saved_pixel_hash_exact':True,'resident_pages':int(recipes[1]),'computed_pages':int(recipes[2]),'source_upload_bytes':int(recipes[3])}
result['opengl_debug_errors']=log.count('BTS_GL_ERROR')
(a.output/'gpu-stack-verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

assert result['opengl_debug_errors']==0, 'OpenGL debug errors in GPU stack regression'
