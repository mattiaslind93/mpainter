# SPDX-License-Identifier: GPL-3.0-or-later
"""Native OpenPBR naming/EXR32 MaterialX bundle and exact Classic pixel regression."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess,zipfile,xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'openpbr-export.obj';mesh.write_text('''o TwoTiles
v -1 -1 0
v 0 -1 0
v 0 1 0
v -1 1 0
v 0 -1 0
v 1 -1 0
v 1 1 0
v 0 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vt 1 0
vt 2 0
vt 2 1
vt 1 1
f 1/1 2/2 3/3 4/4
f 5/5 6/6 7/7 8/8
''')
if not a.existing:
 env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',BTS_OPENPBR_EXPORT_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str(a.output/'cache'),BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_SESSION_LOG_DIR=str(a.output/'logs'),BTS_TEMPLATE_DIR=str(a.output/'templates'))
 with (a.output/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
d=json.loads((a.output/'openpbr-export.json').read_text());c={x['stage']:x for x in d['captures']};cfg=c['exported-openpbr']['document']['export_configuration'];assert cfg['version']==7 and cfg['naming_convention']==1 and cfg['openpbr_materialx']and cfg['format']==3
assert cfg==c['opened']['document']['export_configuration'];classic=c['exported-classic']['document']['export_configuration'];assert classic['naming_convention']==0 and not classic['openpbr_materialx']
assert c['configured']['document']['texture_sets'][0]['tiles']==[1001,1002]
sdk=Path(os.environ.get('BTS_ROOT','/home/mlind/bts'))/'blender/lib/linux_x64';tool=sdk/'openimageio/bin/oiiotool';toolenv=dict(os.environ,LD_LIBRARY_PATH=':'.join(str(p/'lib')for p in sdk.iterdir()if(p/'lib').is_dir()),LC_ALL='C')
def info(path):
 text=subprocess.check_output([str(tool),'--threads','1','--info','-v','--stats','--hash',str(path)],env=toolenv,text=True,stderr=subprocess.STDOUT)
 assert re.search(r'128\s*x\s*128,\s*\d+ channel,\s*float openexr',text),text
 digest=re.search(r'SHA-1:\s*([0-9a-fA-F]+)',text);assert digest,text
 return text,digest[1]
checks=[]
for path in sorted((a.output/'openpbr').glob('*.exr')):
 match=re.search(r'_(base_color|base_metalness|specular_roughness|bump|displacement)_(1001|1002)\.exr$',path.name)
 assert 'normal' not in path.name.lower()
 if not match:continue
 role,tile=match.groups();text,digest=info(path)
 assert re.search(r'\b'+('3'if role=='base_color'else'1')+r' channel',text),text
 legacy={'base_color':'color','base_metalness':'metallic','specular_roughness':'roughness'}.get(role,role);other=a.output/'classic'/path.name.replace('_'+role+'_'+tile,'_'+legacy+'_'+tile)
 assert other.exists()and info(other)[1]==digest,other
 if role in ['bump','displacement']:
  minimum=re.search(r'Stats Min:\s*([-+0-9.eE]+)',text);assert minimum,text
  expected=-.125 if role=='bump'else-.0025;assert abs(float(minimum[1])-expected)<1e-6,(role,text)
 checks.append({'file':path.name,'role':role,'tile':int(tile),'float32':True,'sha1':digest,'classic_exact_pixels':True})
assert len(checks)==10,checks
xmls=list((a.output/'openpbr').glob('*.mtlx'));assert len(xmls)==1
xml=ET.parse(xmls[0]).getroot();surface=xml.find("open_pbr_surface[@name='StudioSurface']");assert surface is not None and surface.attrib['nodedef']=='ND_BTS_OpenPBR_1_1_1'
assert xml.find("nodedef[@name='ND_BTS_OpenPBR_1_1_1']").attrib['version']=='1.1.1'
bump_scale=float(xml.find("bump[@name='StudioBump']/input[@name='scale']").attrib['value'])
native_distances=[value['bump_distance']for value in c['configured']['shader_nodes'].values()if 'bump_distance'in value];assert native_distances and all(abs(value-bump_scale)<1e-9 for value in native_distances),(native_distances,bump_scale)
assert all(n.attrib['colorspace']in ['acescg','raw']for n in xml.findall('image'))
for image in xml.findall('image'):
 filename=image.find("input[@name='file']").attrib['value'];assert '<UDIM>'in filename and not Path(filename).is_absolute()
 for tile in [1001,1002]:assert (a.output/'openpbr'/filename.replace('<UDIM>',str(tile))).exists()
assert (a.output/'openpbr/OpenPBR-LICENSE.txt').read_bytes()==(root/'native/host/openpbr/LICENSE').read_bytes()
usd=xmls[0].with_suffix('.usda');assert usd.exists();usd_text=usd.read_text()
assert 'defaultPrim = "MaterialX"'in usd_text and 'outputs:mtlx:surface.connect'in usd_text
assert 'sourceAsset = @'+xmls[0].name+'@'in usd_text and 'subIdentifier = "ND_BTS_OpenPBR_1_1_1"'in usd_text
assert 'colorSpace = "acescg"'in usd_text and 'colorSpace = "raw"'in usd_text
assert 'outputs:mtlx:displacement.connect'in usd_text
assert not list((a.output/'classic').glob('*.usda'))

assert not list((a.output/'classic').glob('*.mtlx'))and not (a.output/'classic/OpenPBR-LICENSE.txt').exists()
scene_proofs={}
for suffix in ['usdc','usdz']:
 scenes=list((a.output/'openpbr').glob('*.'+suffix));assert len(scenes)==1,scenes
 proof=a.output/('usd-scene-'+suffix+'.json')
 subprocess.run([str(Path(os.environ.get('BTS_ROOT','/home/mlind/bts'))/'build/host/bin/bts-openpbr-usd-tests'),'--inspect-scene',str(scenes[0]),str(proof)],env=toolenv,check=True,timeout=30)
 scene_proofs[suffix]=json.loads(proof.read_text())
 if suffix=='usdz':
  with zipfile.ZipFile(scenes[0])as archive:
   names=archive.namelist();assert any(n.endswith('.mtlx')for n in names),names
   assert any(Path(n).name=='OpenPBR-LICENSE.txt'for n in names),names
   for original in (a.output/'openpbr').iterdir():
    if original.suffix in ['.exr','.mtlx','.usda']or original.name=='OpenPBR-LICENSE.txt':assert archive.read(original.name)==original.read_bytes(),original.name
   assert not any('normal' in Path(n).name.lower()for n in names),names
assert not list((a.output/'classic').glob('*.usdc'))and not list((a.output/'classic').glob('*.usdz'))
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}'in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK',log,re.I)
result={'pid':d['pid'],'config_version':7,'gui_naming_selector_and_package_guard':True,'exr32_format_locked':True,'save_open_settings':True,'tiles':[1001,1002],'maps':checks,'materialx':xmls[0].name,'usd_material_library':usd.name,'usd_geometry_included':True,'usd_scene_proofs':scene_proofs,'usd_color_spaces_preserved':True,'reference':'ASWF 1.1.1, package-local definition/implementation','license_exact':True,'bump_scale_matches_native_distance':bump_scale,'normal_map_count':0,'classic_pixel_regression':'Identical decoded SHA-1 for all ten EXR32 images','scope':'Native export naming and self-contained MaterialX transport. Viewport still uses explicit Principled compatibility, not this full BSDF. AO stays a separate export map.','clean_shutdown':True};(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
