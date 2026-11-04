"""Install checksum-verified NVIDIA CUDA compiler resources under the BTS workspace."""
import argparse,filecmp,hashlib,json,os,shutil,tarfile,urllib.request
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(os.environ.get('BTS_ROOT','/home/mlind/bts'))/'toolchains/cuda-13.2.1');a=p.parse_args();a.root.mkdir(parents=True,exist_ok=True)
base='https://developer.download.nvidia.com/compute/cuda/redist/'
manifest=json.loads(urllib.request.urlopen(base+'redistrib_13.2.1.json',timeout=60).read())
(a.root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
expected={'cuda_crt': '17e731ba749765c2e2e325e3bffecee94226c866cf59f13ba3792aa4f2b1bb31', 'cuda_nvcc': '1dc73b03f1d74081866986ca406f7b5981d14306ccbb03127ba45401f36e1862', 'cuda_cudart': '9502ab2c7824e5ef3b554d290b9f564c5026994f469b99c72451a53578f386b9', 'cuda_cccl': '1801f304d92085a327ab46db58264d7f7f48cce80f5825637c405c3a2410dadd', 'libnvvm': '9c665ebec40d0dec4df1858d5a0129972b00351dd674e783d30405cf712d925d'}
for name,digest in expected.items():
 entry=manifest[name]['linux-x86_64'];
 if entry['sha256']!=digest:raise ValueError(name+': unexpected release manifest')
 archive=a.root/Path(entry['relative_path']).name
 if not archive.exists():
  temporary=archive.with_suffix(archive.suffix+'.part')
  with urllib.request.urlopen(base+entry['relative_path'],timeout=60) as source,temporary.open('wb') as dest:shutil.copyfileobj(source,dest)

  if hashlib.sha256(temporary.read_bytes()).hexdigest()!=digest:raise ValueError(name+': download checksum mismatch')
  temporary.replace(archive)
 if hashlib.sha256(archive.read_bytes()).hexdigest()!=digest:raise ValueError(name+': archive checksum mismatch')
 with tarfile.open(archive) as tar:tar.extractall(a.root/'components',filter='data')
 source=next((a.root/'components').glob(name+'-linux-x86_64-*'))
 for child in source.rglob('*'):
  target=a.root/child.relative_to(source)
  if child.is_dir():target.mkdir(parents=True,exist_ok=True);continue
  if target.is_file() and filecmp.cmp(child,target,shallow=False):continue
  target.parent.mkdir(parents=True,exist_ok=True);temporary=target.with_name(target.name+'.bts-install')
  shutil.copy2(child,temporary);temporary.replace(target)
 print(name,entry['size'],digest,flush=True)
print('CUDA 13.2.1 ready:',a.root,'; use GCC 14 for kernel compilation.')
