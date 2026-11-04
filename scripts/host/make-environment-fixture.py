"""Create a small original HDR fixture with asymmetric colored area lights in stock Blender."""
import bpy,math,sys
from pathlib import Path
path=Path(sys.argv[sys.argv.index('--')+1]);path.parent.mkdir(parents=True,exist_ok=True)
w,h=128,64
image=bpy.data.images.new('BTS environment fixture',width=w,height=h,float_buffer=True)
image.colorspace_settings.name='Linear Rec.709'
pixels=[]
for y in range(h):
    for x in range(w):
        u,v=(x+.5)/w,(y+.5)/h
        a=10*math.exp(-((u-.25)**2/.008+(v-.6)**2/.03))
        b=5*math.exp(-((u-.78)**2/.015+(v-.45)**2/.05))
        pixels.extend([.03+a,.03+.2*a+.4*b,.05+b,1])
image.pixels.foreach_set(pixels)
image.filepath_raw=str(path);image.file_format='OPEN_EXR';image.save()
print('BTS_ENVIRONMENT_FIXTURE',str(path))
