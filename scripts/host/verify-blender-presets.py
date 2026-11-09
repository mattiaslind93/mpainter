# SPDX-License-Identifier: GPL-3.0-or-later
"""Native half of the Blender roundtrip proof. Run inside bts-build/xvfb-run.

Consumes actual Cycles socket bakes from scripts/blender/verify_presets.py.
Uses production codecs/evaluation, checks edits, negative cases and writes images.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import struct
import zlib
import numpy as np
from PIL import Image, ImageDraw

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--native',type=Path,required=True);p.add_argument('--catalog',type=Path,required=True)
p.add_argument('--validation',type=Path,required=True);p.add_argument('--shots',type=Path,required=True)
a=p.parse_args()
if not os.environ.get('DISPLAY') or not Path('/run/.containerenv').exists(): raise RuntimeError('Run inside distrobox bts-build under xvfb-run')
a.shots.mkdir(parents=True,exist_ok=True)
report=json.loads((a.validation/'blender-results.json').read_text())
records=[]

def run(*args, okay=True):
    result=subprocess.run([str(a.native),*map(str,args)],capture_output=True,text=True)
    if okay and result.returncode: raise RuntimeError(result.stderr+result.stdout)
    if not okay and result.returncode==0: raise AssertionError('Invalid input accepted')
    return result

def floats(directory,desc):return np.fromfile(directory/desc['file'],dtype='<f4').reshape(desc['height'],desc['width'],4)
def metric(left,right):
    d=np.abs(left[...,:3]-right[...,:3]);return {'mean':float(d.mean()),'maximum':float(d.max()),'rms':float(np.sqrt((d*d).mean()))}
def display(values):
    rgb=np.clip(values[...,:3],0,1)
    rgb=np.where(rgb<=.0031308,12.92*rgb,1.055*rgb**(1/2.4)-.055)
    return Image.fromarray(np.uint8(np.clip(rgb[::-1]*255+.5,0,255)))
def pair(name,left,right,delta,path,labels=('mPainter CPU','Blender Cycles nodes')):
    w,h=left.size
    sheet=Image.new('RGB',(w*3,h+60),'#202323');draw=ImageDraw.Draw(sheet)
    for i,(image,label) in enumerate(zip((left,right,delta),(*labels,'Absolute difference x10'))):
        sheet.paste(image,(i*w,40));draw.text((i*w+8,22),label,fill='white')
    draw.text((8,5),name,fill='white');sheet.save(path)

for original in sorted(a.catalog.glob('*/preset.json')):
    data=json.loads(original.read_text());slug=data['name'];directory=a.validation/slug
    ext='.btsmask' if data['kind']=='mask' else '.btsmaterial'
    entry={'preset':slug,'kind':data['kind']}
    for variant in ('unchanged','edited'):
        archive=directory/(variant+ext);run('import',directory/(variant+'.json'),archive)
        if variant=='unchanged':
            entry['archive_byte_identical']=archive.read_bytes()==(a.catalog/(slug+ext)).read_bytes()
            if data['kind']=='mask':assert entry['archive_byte_identical'],slug+' unchanged archive bytes differ'
        exported=directory/(variant+'-native');run('export',archive,exported)
        restored=json.loads((exported/'preset.json').read_text())
        if data['kind']=='mask':
            desc=restored['mask']['reference'];ref=data['mask']['reference'];channel='mask'
            if variant=='unchanged':
                assert restored['mask']['effects']==data['mask']['effects'],slug+' metadata changed'
                assert restored['mask']['background']==data['mask']['background']
        else:
            desc=restored['reference_roughness'];ref=data['reference_roughness'];channel='roughness'
            if variant=='unchanged':
                for item in data['layers']:
                    if item.get('mask'):item['mask'].setdefault('has_paint',False)
                assert restored['layers']==data['layers'],slug+' material metadata changed'
        actual=floats(exported,desc)
        if variant=='unchanged':assert np.array_equal(actual,floats(original.parent,ref)),slug+' native roundtrip changed pixels'
        blender=np.fromfile(directory/('blender-'+channel+'.f32' if variant=='unchanged' else 'edited-blender.f32'),dtype='<f4').reshape(256,256,4)
        error=metric(actual,blender);entry[variant]=error
        if variant=='edited':
            change=metric(actual,floats(original.parent,ref));entry['edit_effect']=change
            assert change['maximum']>1e-4,slug+' exported control had no observable effect'
        # Tolerances are for float node evaluation, not display-encoded pixels.
        # Checked after recording results below so failures retain diagnostics.
        delta=np.clip(np.abs(actual[...,:3]-blender[...,:3])*10,0,1)
        pair(slug+' / '+variant,display(actual),display(blender),Image.fromarray(np.uint8(delta[::-1]*255+.5)),a.shots/(slug+'-'+variant+'.png'))
        if data['kind']=='mask' and variant=='unchanged':
            desc={'file':'blender-mask.f32','width':256,'height':256,'channels':4,'origin':'bottom-left','encoding':'float32-le-linear'}
            (directory/'bake.json').write_text(json.dumps({'bake_version':1,'kind':'mask','mask':desc}))
            run('import-bake',directory/'bake.json',directory/'blender-bake.btsmask')
            run('export',directory/'blender-bake.btsmask',directory/'blender-bake-native')
            left=Image.open(original.parent/'mask-sphere.png').convert('RGB')
            right=Image.open(directory/'blender-bake-native/mask-sphere.png').convert('RGB')
            delta=np.clip(np.abs(np.array(left).astype(float)-np.array(right).astype(float))*10,0,255).astype('uint8')
            pair(slug,left,right,Image.fromarray(delta),a.shots/(slug+'-sphere.png'),('mPainter CPU','Blender nodes / same display'))
    # Malformed native edits must fail atomically and preserve existing output.
    bad=json.loads((directory/'edited.json').read_text());bad['bridge_version']=999
    (directory/'invalid.json').write_text(json.dumps(bad));sentinel=directory/'protected.btsmask';sentinel.write_bytes(b'unchanged sentinel')
    run('import',directory/'invalid.json',sentinel,okay=False);assert sentinel.read_bytes()==b'unchanged sentinel';sentinel.unlink()
    entry['atomic_rejection']=True;records.append(entry)
    print(json.dumps(entry),flush=True)

for kind in ('mask','material'):
    directory=a.validation/('arbitrary-'+kind);ext='.btsmask' if kind=='mask' else '.btsmaterial'
    run('import-bake',directory/'bake.json',directory/('baked'+ext));run('export',directory/('baked'+ext),directory/'native')
    restored=json.loads((directory/'native/preset.json').read_text());source=json.loads((directory/'bake.json').read_text());errors={}
    for channel in (['mask'] if kind=='mask' else ['base_color','roughness','metallic']):
        desc=restored['mask']['reference'] if kind=='mask' else restored['reference_'+('color' if channel=='base_color' else channel)]
        actual=floats(directory/'native',desc);expected=floats(directory,source[channel]);error=metric(actual,expected);errors[channel]=error
        assert error['maximum']<1e-6,('arbitrary bake changed',kind,channel,error)
        pair('Blender authored '+kind+' / '+channel,display(actual),display(expected),Image.new('RGB',(256,256)),a.shots/('arbitrary-'+kind+'-'+channel+'.png'),('mPainter imported bake','Blender authored graph'))
    records.append({'preset':'arbitrary-'+kind,'bake_errors':errors})

# A real nested native mask exercises the whole-preset snapshot fallback and
# retention of textures bound to child effect UUIDs (not just a fake flag).
grouped=a.validation/'grouped';grouped.mkdir(exist_ok=True)
raw=zlib.decompress((a.catalog/'oxide-flecks.btsmask').read_bytes()[4:])
magic,length=struct.unpack_from('<II',raw);metadata=json.loads(raw[8:8+length])
metadata['effects']=[{'id':'e546421a-54df-4d1a-abaf-560977137ee6','type':'group','enabled':True,
                      'opacity':1,'blending':0,'effects':metadata['effects']}]
header=json.dumps(metadata,separators=(',',':')).encode();body=struct.pack('<II',magic,len(header))+header+raw[8+length:]
(grouped/'source.btsmask').write_bytes(struct.pack('>I',len(body))+zlib.compress(body,1))
run('export',grouped/'source.btsmask',grouped/'native')
run('import',grouped/'native/preset.json',grouped/'source.btsmask')
run('export',grouped/'source.btsmask',grouped/'native')
report['native_roundtrips']=records
(a.shots/'results.json').write_text(json.dumps(report,indent=2)+'\n')
for suffix,title in [('sphere','Ten built-in masks: mPainter CPU | Blender nodes | difference x10'),('unchanged','UV evaluation: mPainter CPU | Blender nodes | difference x10')]:
    paths=sorted(a.shots.glob('*-'+suffix+'.png'))
    sheet=Image.new('RGB',(1536,316*((len(paths)+1)//2)+40),'#202323');ImageDraw.Draw(sheet).text((12,12),title,fill='white')
    for i,path in enumerate(paths):sheet.paste(Image.open(path),((i%2)*768,(i//2)*316+40))
    sheet.save(a.shots/('comparison-'+suffix+'.png'))
for row in report['records']:
    assert row['mean_absolute_error']<1e-6 and row['maximum_absolute_error']<1e-4,row
for row in records:
    for variant in ('unchanged','edited'):
        if variant in row: assert row[variant]['mean']<1e-6 and row[variant]['maximum']<1e-4,row
print('PASS: native codec/evaluation roundtrips, actual Blender edits, arbitrary graph bakes, atomic rejection and visual comparisons')
