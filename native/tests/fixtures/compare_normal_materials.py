from pathlib import Path
import sys
from PIL import Image, ImageDraw
from collections import deque
base=Path(sys.argv[1])
names=('image-normal','procedural-bump','bump-normal-chain','coat-normal')
sheet=Image.new('RGB',(860,1800),'#202526'); draw=ImageDraw.Draw(sheet)
draw.text((20,15),'Blender Cycles CPU reference       |       mPainter EEVEE RTX 3090',fill='white')
draw.text((20,38),'Visual comparison: different studio lighting and camera; same exported sphere mesh; no pixel parity claim.',fill='#b1c6c7')
for row,name in enumerate(names):
    left=Image.open(base/'fixtures'/f'{name}-blender.png').convert('RGB')
    right=Image.open(base/'results'/f'{name}-viewport.png').convert('RGB')
    reduced=right.resize((250,175))
    pixels=list(reduced.getdata()); remaining={i for i,(r,g,b) in enumerate(pixels) if g>r*1.2 and b>r*1.2 and g>30}
    largest=[]
    while remaining:
        seed=remaining.pop(); queue=deque([seed]); group=[seed]
        while queue:
            value=queue.popleft();x=value%250;y0=value//250
            for dx,dy in ((-1,0),(1,0),(0,-1),(0,1)):
                xx,yy=x+dx,y0+dy
                neighbor=yy*250+xx
                if 0<=xx<250 and 0<=yy<175 and neighbor in remaining:
                    remaining.remove(neighbor);queue.append(neighbor);group.append(neighbor)
        if len(group)>len(largest):largest=group
    if largest:
        x0=min(i%250 for i in largest)*4;y0=min(i//250 for i in largest)*4
        x1=(max(i%250 for i in largest)+1)*4;y1=(max(i//250 for i in largest)+1)*4
        size=max(x1-x0,y1-y0);cx=(x0+x1)/2;cy=y0+size/2; margin=25
        right=right.crop((max(0,int(cx-size/2-margin)),max(0,int(cy-size/2-margin)),min(right.width,int(cx+size/2+margin)),min(right.height,int(cy+size/2+margin))))
    y=75+row*425; draw.text((20,y),name,fill='#8be3d5')
    for x,img in ((20,left),(450,right)):
        img.thumbnail((390,390),Image.Resampling.LANCZOS)
        if img.width<390 and img.height<390:
            scale=min(390/img.width,390/img.height);img=img.resize((round(img.width*scale),round(img.height*scale)),Image.Resampling.LANCZOS)
        sheet.paste(img,(x+(390-img.width)//2,y+25+(390-img.height)//2))
    panel=sheet.crop((0,y,860,y+425));panel.save(base/'results'/f'{name}-comparison.png')
sheet.save(base/'results'/'normal-import-comparison.png')
print('Wrote four side-by-side comparisons and normal-import-comparison.png')
