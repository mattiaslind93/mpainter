# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent PNG/OpenEXR header and uncompressed scanline reader for export proofs."""
import struct
import zlib

def png_pixels(path):
    b=path.read_bytes();assert b[:8]==b'\x89PNG\r\n\x1a\n';offset=8;packed=b''
    while offset<len(b):
        size=struct.unpack_from('>I',b,offset)[0];kind=b[offset+4:offset+8];value=b[offset+8:offset+8+size];offset+=size+12
        if kind==b'IHDR':width,height,depth,color,compression,filtering,interlace=struct.unpack('>IIBBBBB',value)
        if kind==b'IDAT':packed+=value
    assert depth in (8,16) and color in (0,2,4,6) and interlace==0
    channels={0:1,2:3,4:2,6:4}[color];bpp=channels*(depth//8);stride=width*bpp;raw=zlib.decompress(packed);rows=[];previous=bytearray(stride)
    def paeth(a,b,c):
        p=a+b-c;dist=[abs(p-a),abs(p-b),abs(p-c)];return [a,b,c][dist.index(min(dist))]
    for y in range(height):
        flag=raw[y*(stride+1)];row=bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for i in range(stride):
            a=row[i-bpp] if i>=bpp else 0;b=previous[i];c=previous[i-bpp] if i>=bpp else 0
            predictor=[0,a,b,(a+b)//2,paeth(a,b,c)][flag];row[i]=(row[i]+predictor)&255
        rows.append(row);previous=row
    def sample(x,y):
        row=rows[height-1-y];offset=x*bpp
        return [row[offset+c]/255 if depth==8 else struct.unpack_from('>H',row,offset+2*c)[0]/65535 for c in range(channels)]
    return {'width':width,'height':height,'bits':depth,'channels':channels,'sample':sample}

def exr_pixels(path):
    b=path.read_bytes();assert struct.unpack_from('<I',b)[0]==20000630;offset=8;attrs={}
    def cstring(offset):
        end=b.index(b'\0',offset);return b[offset:end].decode(),end+1
    while b[offset]!=0:
        name,offset=cstring(offset);kind,offset=cstring(offset);size=struct.unpack_from('<I',b,offset)[0];offset+=4;attrs[name]=(kind,b[offset:offset+size]);offset+=size
    offset+=1;channels=[];raw=attrs['channels'][1];at=0
    while raw[at]:
        end=raw.index(b'\0',at);name=raw[at:end].decode();at=end+1;typ=struct.unpack_from('<i',raw,at)[0];at+=16;channels.append((name,typ))
    minimum_x,minimum_y,maximum_x,maximum_y=struct.unpack('<4i',attrs['dataWindow'][1]);assert minimum_x==minimum_y==0
    width=maximum_x+1;height=maximum_y+1;assert attrs['compression'][1]==b'\0', 'Reader expects native uncompressed EXR'
    offsets=struct.unpack_from('<'+'Q'*height,b,offset)
    def sample(x,y):
        row=height-1-y;at=offsets[row];scanline,size=struct.unpack_from('<ii',b,at);assert scanline==row;at+=8;values={}
        for name,typ in channels:
            count=2 if typ==1 else 4;values[name]=struct.unpack_from('<e' if typ==1 else '<f',b,at+x*count)[0];at+=width*count
        return [values[name] for name in ('R','G','B') if name in values]
    return {'width':width,'height':height,'types':dict(channels),'channels':len(channels),'sample':sample}
