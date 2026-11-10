"""Build the experimental R1 pixels solely from the curated Kenney CC0 member."""
from pathlib import Path
import argparse
import hashlib
import json


def pixels(source):
    from PIL import Image
    image = Image.open(source).convert('RGBA')
    if image.size != (64, 64):
        raise ValueError('The inspected R1 member must be 64 by 64 pixels')
    atlas = Image.new('RGBA', (512, 192))
    atlas.paste(image, (384, 64))
    return atlas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = root / 'assets/appendage-glyph/playstation_trigger_r1.png'
    record = json.loads((source.parent / 'provenance.json').read_text())
    assert hashlib.sha256(source.read_bytes()).hexdigest() == record['asset_sha256']
    assert hashlib.sha256((source.parent / 'License.txt').read_bytes()).hexdigest() == record['license_sha256']
    data = pixels(source).tobytes()
    # Store only nonzero texels. The runtime's owned surface is otherwise transparent.
    texels = [(i // 4, data[i:i+4]) for i in range(0, len(data), 4) if any(data[i:i+4])]
    lines = ['// Generated from the Kenney CC0 R1 member by tools/make_appendage_glyph.py.',
             '// No game-derived pixels. See assets/appendage-glyph/provenance.json.',
             'static const struct { uint32_t pixel, rgba; } appendage_r1_texels[] = {']
    lines += [f'    {{{i}, 0x{int.from_bytes(value, "little"):08x}u}},' for i, value in texels]
    lines += ['};', '']
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text('\n'.join(lines))
    print(f'{len(texels)} nonzero texels; surface SHA256 {hashlib.sha256(data).hexdigest()}')


if __name__ == '__main__':
    main()
