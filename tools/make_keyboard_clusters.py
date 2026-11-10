"""Compose the keyboard cluster tiles (arrow keys, WASD, 1-4) from single Kenney key tiles.

The four-direction clusters stand for a stick or the D-pad bound to keys; the two-key tiles stand for its vertical or horizontal
pair. Each one is a derivative of pinned CC0 members of the Kenney Input Prompts package: the members and their hashes are
recorded in assets/appendage-glyph/styles/provenance.json. Run once; it is idempotent and does not touch other tiles.
"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

# (name, art input prefix, tile of the left, up, right and down key)
CLUSTERS = (
    ('arrows', 'GLYPH_IN_KB_ARROWS', ('keyboard_arrow_left', 'keyboard_arrow_up', 'keyboard_arrow_right', 'keyboard_arrow_down')),
    ('wasd', 'GLYPH_IN_KB_WASD', ('keyboard_a', 'keyboard_w', 'keyboard_d', 'keyboard_s')),
    ('1234', 'GLYPH_IN_KB_NUM', ('keyboard_1', 'keyboard_2', 'keyboard_3', 'keyboard_4')),
)
SHAPES = (('', ''), ('_vertical', '_V'), ('_horizontal', '_H'))   # four keys, up/down pair, left/right pair
T = 64


def key_image(src):
    image = Image.open(src).convert('RGBA')
    assert image.size == (T, T)
    return image.crop(image.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox())


def compose(keys, shape):
    left, up, right, down = keys
    canvas = Image.new('RGBA', (T, T), (0, 0, 0, 0))

    def put(key, size, x, y):
        canvas.alpha_composite(key.resize((size, size), Image.LANCZOS), (x, y))
    if shape == 0:      # the inverted T of a cursor block: three keys below one
        put(up, 20, 22, 11), put(left, 20, 0, 33), put(down, 20, 22, 33), put(right, 20, 44, 33)
    elif shape == 1:
        put(up, 28, 18, 0), put(down, 28, 18, 32)
    else:
        put(left, 28, 0, 18), put(right, 28, 32, 18)
    return canvas


def dimmed(key):
    key = key.copy()
    key.putalpha(key.getchannel('A').point(lambda a: a * 38 // 100))
    return key


def compose_lit_horizontal(keys):
    """The four-key arrow cluster with up and down dimmed, so that only left and right look pressed (the page-turn circles)."""
    left, up, right, down = keys
    canvas = Image.new('RGBA', (T, T), (0, 0, 0, 0))
    for key, size, x, y in ((dimmed(up), 20, 22, 11), (left, 20, 0, 33), (dimmed(down), 20, 22, 33), (right, 20, 44, 33)):
        canvas.alpha_composite(key.resize((size, size), Image.LANCZOS), (x, y))
    return canvas


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kenney', type=Path, required=True, help='the extracted Kenney Input Prompts 1.5A folder')
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    dst = root / 'assets/appendage-glyph/styles'
    prov = json.loads((dst / 'provenance.json').read_text())
    zip_path = a.kenney.parent / (a.kenney.name + '.zip')
    assert hashlib.sha256(zip_path.read_bytes()).hexdigest() == prov['package_sha256'], 'unexpected Kenney package'
    folder = a.kenney / 'Keyboard & Mouse' / 'Default'
    assets = [x for x in prov['assets'] if not x.get('derived')]
    for name, prefix, names in CLUSTERS:
        for shape, (suffix, enum) in enumerate(SHAPES):
            file = f'keyboard_cluster_{name}{suffix}.png'
            compose([key_image(folder / f'{n}.png') for n in names], shape).save(dst / file)
            used = (names, names[1::2] if shape == 1 else (names[0], names[2]) if shape == 2 else names)[shape > 0]
            assets.append({'style': 'KEYBOARD', 'input': prefix + enum, 'derived': True, 'file': file,
                           'members': [{'member': f'Keyboard & Mouse/Default/{n}.png',
                                        'sha256': hashlib.sha256((folder / f'{n}.png').read_bytes()).hexdigest()} for n in used],
                           'sha256': hashlib.sha256((dst / file).read_bytes()).hexdigest()})
    # Shift (the default B key) is one more plain pinned Kenney tile; it comes after the 128 tiles the HUD route can address.
    shift = folder / 'keyboard_shift.png'
    if not any(x['file'] == 'keyboard_shift.png' for x in assets):
        (dst / 'keyboard_shift.png').write_bytes(shift.read_bytes())
        assets.insert(sum(1 for x in assets if not x.get('derived')), {
            'style': 'KEYBOARD', 'input': 'KEY_LSHIFT', 'member': 'Keyboard & Mouse/Default/keyboard_shift.png',
            'sha256': hashlib.sha256(shift.read_bytes()).hexdigest(), 'file': 'keyboard_shift.png'})
    names = CLUSTERS[0][2]
    file = 'keyboard_cluster_arrows_lit_horizontal.png'
    compose_lit_horizontal([key_image(folder / f'{n}.png') for n in names]).save(dst / file)
    assets.append({'style': 'KEYBOARD', 'input': 'GLYPH_IN_KB_ARROWS_LIT_H', 'derived': True, 'file': file,
                   'note': 'the arrow cluster with the up and down keys at 38% opacity',
                   'members': [{'member': f'Keyboard & Mouse/Default/{n}.png', 'sha256': hashlib.sha256((folder / f'{n}.png').read_bytes()).hexdigest()}
                               for n in names], 'sha256': hashlib.sha256((dst / file).read_bytes()).hexdigest()})
    prov['assets'] = assets
    (dst / 'provenance.json').write_text(json.dumps(prov, indent=2) + '\n')
    print(len(assets), 'tiles in the manifest')


if __name__ == '__main__':
    main()
