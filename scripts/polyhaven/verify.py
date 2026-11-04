#!/usr/bin/env python3
"""Validate shipped bytes, native graph closure and bounded UI receipts."""
import argparse, base64, hashlib, json, math, struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--ui',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
rows=json.loads((a.library/'manifest.json').read_text());assert len(rows)==47
assert len({r['name'] for r in rows})==47
slugs=[r['polyhaven_slug'] for r in rows if r['maps']];assert len(slugs)==len(set(slugs))==30
assert all('sp_name' not in r for r in rows)
sha=lambda f:hashlib.file_digest(f.open('rb'),'sha256').hexdigest()
source_bytes=0;maps=0;archives=0;warnings={};source_hashes=set()
for r in rows:
 metadata=json.loads((a.library/r['native_metadata']).read_text())
 assert metadata['source_inputs_preserved'] and not metadata['world_normal_error'],r['name']
 assert sha(a.library/r['native_archive'])==metadata['material']['sha256'],r['name']
 data=(a.library/r['native_archive']).read_bytes();assert data[:8]==b'MFPNAT1\n'
 descriptor=json.loads(data[24:24+struct.unpack('>Q',data[8:16])[0]])
 preview=base64.b64decode(descriptor['preview_png']);assert preview[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',preview[16:24])==(256,256),r['name']
 archives+=(a.library/r['native_archive']).stat().st_size
 assert all(c['supported'] for c in metadata['channels'] if c['channel'] in [0,1,2,5,14]),r['name']
 assert metadata['preview']['mesh']=='Shader_Ball_JL_01'
 warnings[r['name']]=metadata['material']['native_material']['warnings']
 deps={f['sha256'] for t in metadata['textures'] for f in t['metadata']['texture_files']}
 for rec in r['maps'].values():
  files=list((a.library/'native/images').glob(rec['sha256']+'.*'));assert len(files)==1,rec['file'];f=files[0]
  assert f.stat().st_size==rec['size'] and sha(f)==rec['sha256'],rec['file']
  assert hashlib.file_digest(f.open('rb'),'md5').hexdigest()==rec['md5'],rec['file']
  assert rec['sha256'] in deps,('unused original map',r['name'],rec['file'])
  maps+=1;source_bytes+=f.stat().st_size;source_hashes.add(rec['sha256'])
 assert deps=={rec['sha256'] for rec in r['maps'].values()},r['name']
assert len(list((a.library/'native/images').iterdir()))==len(source_hashes)
proof=dict(materials=47,textured=30,procedural=17,maps=maps,texture_bytes=source_bytes,native_archive_bytes=archives,library_bytes=sum(f.stat().st_size for f in a.library.rglob('*') if f.is_file()),unique_texture_sources=len(source_hashes),original_inputs_preserved=True,world_normal_graphs_supported=True,all_source_checksums_verified=True,embedded_previews_256px=True,warnings=warnings)
if a.ui:
 ui=json.loads((a.ui/'validation.json').read_text());assert ui['material_count']==47
 detail=ui['resolution']==1024
 assert detail or ui['resolution']==128
 checks=ui['checks'];assert len(checks)==(3 if detail else 8)
 for check in checks:
  mode=1 if check['stage'].endswith('-triplanar') else 0
  assert check['native'] and check['authored_layer']['fill_projection']['mode']==mode,check['stage']
  assert check['dimensions']==[[1001,ui['resolution'],ui['resolution']]],check['stage']
  for packet in check['gpu']:
   for point in packet['points']:
    assert all(math.isfinite(x) for x in point['rgba']),check['stage']
 # Real textured/procedural materials must change evaluated samples with projection.
 for name in (['detail'] if detail else ['denim','carbon','cherry']):
  before=next(c for c in checks if c['stage']==name+'-uv')['gpu'];after=next(c for c in checks if c['stage']==name+'-triplanar')['gpu'];assert before!=after,name
 assert ui['packed_open'], 'No independent packed source / mapping comparison'
 if detail:assert ui['default_resolution']
 proof['ui']=dict(materials_tested=[ui['material']] if detail else ['Gold','Denim','Carbon Fibre','Cherry Wood'],uv_and_triplanar=True,projection_changes_gpu_samples=True,packed_open=ui['packed_open'],resolution=ui['resolution'])
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:v for k,v in proof.items() if k!='warnings'},indent=2))
