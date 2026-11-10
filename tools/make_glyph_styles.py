"""Reproduce only licensed glyph tiles (Kenney CC0, Audune CC BY-SA 4.0); no guest resource pixels or runtime game decoding."""
import argparse
from pathlib import Path
import hashlib
import json
from compression import zstd
from PIL import Image


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    source=root/'assets/appendage-glyph/styles'
    manifest=json.loads((source/'provenance.json').read_text())
    blob=bytearray();rows=[]
    for asset in manifest['assets']:
        path=(root/manifest['audune']['folder'] if asset.get('pack')=='audune' else source)/asset['file'];assert hashlib.sha256(path.read_bytes()).hexdigest()==asset['sha256']
        image=Image.open(path).convert('RGBA');assert image.size==(64,64)
        packed=zstd.compress(image.tobytes(),level=19)
        rows.append((asset,len(blob),len(packed)));blob.extend(packed)
    assert len(rows)<=256, "art ids are 16-bit but the HUD route addresses only the first 128"
    lines=['// Licensed glyph tiles. Reproduce with tools/make_glyph_styles.py. Provenance: assets/appendage-glyph/styles/provenance.json.',
           '// Keyboard and Steam tiles: Kenney Input Prompts 1.5A, CC0-1.0.',
           '// Xbox and PlayStation tiles: Audune Prompts 1.3.1 by Audune, licensed CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/),',
           '// https://audune.itch.io/audune-prompts, cut from the sprite sheets (the PlayStation colored faces are recolored);',
           '// see assets/appendage-glyph/audune/NOTICE.txt. No game pixels.',
           'static const struct { int style, input, appearance; unsigned offset, length; const char *name; } glyph_arts[] = {']
    for asset,off,size in rows:lines.append(f'    {{GLYPH_{asset["style"]}, {asset["input"]}, GLYPH_{asset.get("appearance","STANDARD")}, {off}, {size}, "{asset["file"]}"}},')
    lines+=['};','static const unsigned char glyph_art_bytes[] = {']
    for i in range(0,len(blob),20):lines.append('    '+','.join(f'0x{x:02x}' for x in blob[i:i+20])+',')
    lines+=['};','']
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text('\n'.join(lines))
    print(len(rows),'licensed tiles;',len(blob),'compressed bytes')


if __name__=='__main__':main()
