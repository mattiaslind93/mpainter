#!/usr/bin/env python3
"""Normalize build metadata and use neutral color-space display names.

Interoperability aliases remain unchanged so imported files keep their color meaning.
"""
from pathlib import Path
import re,sys

def sanitize(root):
    for shader in root.rglob('*.oso'):
        text=shader.read_text()
        text=re.sub(r'^# (?:options:|/).*$', '# Build-source metadata omitted.', text, flags=re.M)
        text=re.sub(r'(?<=%filename\{)"/[^"\n]+"',lambda m:'"'+m[0].strip('"').rsplit('/',1)[-1]+'"',text)
        shader.write_text(text)
    for config in root.rglob('config.ocio'):
        text=config.read_text()
        for original,display in [('Linear Adobe RGB','Linear Wide Gamut RGB (1998)'),
                                 ('Gamma 2.2 Encoded AdobeRGB','Gamma 2.2 Wide Gamut RGB (1998)')]:
            text=text.replace('name: '+original,'name: '+display)
            # Keep old serialized color-space names resolvable after changing the label.
            pattern=r'(name: '+re.escape(display)+r'\n\s+aliases: \[)([^\]]*)(\])'
            def aliases(match):
                existing=[value.strip().strip('"') for value in match[2].split(',')]
                return match[0] if original in existing else match[1]+match[2]+', "'+original+'"'+match[3]
            text=re.sub(pattern,aliases,text)
        config.write_text(text)

if __name__=='__main__':sanitize(Path(sys.argv[1]))
