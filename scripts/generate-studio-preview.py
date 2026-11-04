#!/usr/bin/env python3
"""Bake linear RGBE thumbnail lighting from the bundled, unrotated Studio HDR.

Run with numpy, Pillow and opencv-python-headless. Rotation belongs to the
sampler, exactly as in EEVEE/Cycles; these textures introduce no flip or offset.
"""
from pathlib import Path
import hashlib
import cv2
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1]
source = root / 'assets/environments/default-studio.hdr'
out = root / 'assets/mask-preview'
hdr = cv2.imread(str(source), cv2.IMREAD_UNCHANGED)[..., ::-1]
w, h = 256, 128
u, v = np.meshgrid((np.arange(w)+.5)/w, (np.arange(h)+.5)/h)
phi, theta = (.5-u)*2*np.pi, v*np.pi
normal = np.stack((np.sin(theta)*np.cos(phi), np.sin(theta)*np.sin(phi), np.cos(theta)), -1)
tangent = np.stack((-np.sin(phi), np.cos(phi), np.zeros_like(phi)), -1)
bitangent = np.cross(normal, tangent)

def sample(direction):
    uv = np.stack(((.5-np.arctan2(direction[...,1],direction[...,0])/(2*np.pi))%1,
                   np.arccos(np.clip(direction[...,2],-1,1))/np.pi), -1)
    # Horizontal padding makes bilinear interpolation continuous at the seam.
    padded = np.concatenate((hdr[:,-1:],hdr,hdr[:,:1]),1)
    return cv2.remap(padded,(uv[...,0]*hdr.shape[1]+.5).astype('float32'),
                     (uv[...,1]*hdr.shape[0]-.5).astype('float32'),cv2.INTER_LINEAR,
                     borderMode=cv2.BORDER_REPLICATE)

def save(rgb, name):
    mantissa, exponent = np.frexp(np.max(rgb,axis=-1))
    scale = np.ldexp(np.ones_like(exponent,dtype=float),8-exponent)
    rgbe = np.concatenate((np.clip(rgb*scale[...,None],0,255).astype('uint8'),
                           np.where(mantissa>1e-32,exponent+128,0)[...,None].astype('uint8')),-1)
    Image.fromarray(rgbe).save(out/name)

for level in range(9):
    if level == 0:
        rgb = sample(normal)
    else:
        rgb = np.zeros((h,w,3));weight = np.zeros((h,w))
        for i in range(512):
            xi = (i+.5)/512
            angle = 2*np.pi*((i*.6180339887498949)%1)
            if level == 8: # cosine-weighted diffuse hemisphere
                cosine = np.sqrt(1-xi)
            else: # GGX importance sampling, V=N
                a2 = (level/7)**4
                cosine = np.sqrt((1-xi)/(1+(a2-1)*xi))
            sine = np.sqrt(1-cosine*cosine)
            half = tangent*(sine*np.cos(angle))+bitangent*(sine*np.sin(angle))+normal*cosine
            direction = half if level==8 else 2*cosine*half-normal
            ndotl = np.ones((h,w)) if level==8 else np.maximum(0,np.sum(normal*direction,axis=-1))
            rgb += sample(direction)*ndotl[...,None];weight += ndotl
        rgb /= weight[...,None]
    save(rgb,f'preview-studio-{level}.png')
print('Studio preview source SHA256:',hashlib.sha256(source.read_bytes()).hexdigest())
