"""Cut the Xbox and PlayStation tiles out of the Audune Prompts sprite sheets.

Audune Prompts 1.3.1 (CC BY-SA 4.0, https://audune.itch.io/audune-prompts) ships two 768x1408 sheets of 64x64 cells. This tool reads
the pinned package, cuts the cells the glyph styles need from the "Light" sheet (white glyphs, readable on the game's dark menus),
and rewrites the Xbox and PlayStation rows of assets/appendage-glyph/styles/provenance.json in place, so every tile keeps its art
id. Keyboard and Steam tiles stay Kenney (CC0).

Changes made to Audune's pixels, recorded per tile in the manifest and in assets/appendage-glyph/audune/NOTICE.txt:
  * every tile is one cell cut out of a sheet, unscaled;
  * the D-pad tiles that name directions (up, down, left, right, up/down, left/right) swap the white and the dim arms, so the
    pressed directions are the bright ones; Audune draws them dim;
  * the four PlayStation "colored" face tiles recolor the dark symbol of the Light-sheet tile to the symbol color that the
    "Dark" sheet gives the same button; nothing else is repainted.

Run once with the downloaded package; it is idempotent. Needs Pillow:  python tools/make_audune_tiles.py --zip audune-prompts-1.3.1.zip
"""
import argparse
import hashlib
import io
import json
import zipfile
from pathlib import Path
from PIL import Image

PACKAGE_SHA256 = '9b94d0cf744b73f2d43ca328d8ebf6ad14f382ec4d04f1a03543a074db4cf55c'
LIGHT, DARK = 'Audune Prompts Light.png', 'Audune Prompts Dark.png'
CELL = 64

# input -> (row, column) of a pad style's block in the sheets. Rows: 13-15 Xbox, 16-18 PlayStation, 10 the generic pad, 19 Switch.
def block(face_row, trig_row, stick_row, dpad_row):
    """Cells of one controller block; the stick row holds L (7 cells) then R (5), the next row R (2) then the D-pad (7)."""
    return {
        'PAD_L': (trig_row, 6), 'PAD_R': (trig_row, 7), 'PAD_ZL': (trig_row, 8), 'PAD_ZR': (trig_row, 9),
        'PAD_LSTICK': (trig_row, 10), 'PAD_RSTICK': (trig_row, 11),
        'GLYPH_IN_STICK_L': (stick_row, 0), 'GLYPH_IN_STICK_L_H': (stick_row, 5), 'GLYPH_IN_STICK_L_V': (stick_row, 6),
        'GLYPH_IN_STICK_R': (stick_row, 7), 'GLYPH_IN_STICK_R_H': (dpad_row - 1, 0), 'GLYPH_IN_STICK_R_V': (dpad_row - 1, 1),
        'GLYPH_IN_DPAD': (dpad_row - 1, 2), 'PAD_DPAD_LEFT': (dpad_row - 1, 3), 'PAD_DPAD_RIGHT': (dpad_row - 1, 4),
        'PAD_DPAD_UP': (dpad_row - 1, 5), 'PAD_DPAD_DOWN': (dpad_row - 1, 6),
        'GLYPH_IN_DPAD_H': (dpad_row - 1, 7), 'GLYPH_IN_DPAD_V': (dpad_row - 1, 8),
    }


CELLS = {
    'XBOX': dict(block(13, 13, 14, 16), PAD_START=(13, 4), PAD_BACK=(13, 5)),
    # Row 16 of the Light sheet holds the DualShock 4's Options (three lines, column 4) and Share (rays, column 5) buttons
    # next to the face buttons; they stand for + and - (the Nintendo plus and minus sit where Options and Share sit).
    'PLAYSTATION': dict(block(16, 16, 17, 19), PAD_START=(16, 4), PAD_BACK=(16, 5)),
}
# The face buttons: (standard, colored) cells for south, east, west, north.
FACES = {
    # Standard Xbox faces are the Switch set's plain letters A, B, X, Y on a white disc; colored ones are Audune's Xbox tiles.
    'XBOX': {'PAD_SOUTH': ((19, 1), (13, 0)), 'PAD_EAST': ((19, 0), (13, 1)), 'PAD_WEST': ((19, 3), (13, 2)), 'PAD_NORTH': ((19, 2), (13, 3))},
    # Standard PlayStation faces are the Light sheet's own; colored ones recolor them (see the module header).
    'PLAYSTATION': {'PAD_SOUTH': ((16, 0), (16, 0)), 'PAD_EAST': ((16, 1), (16, 1)),
                    'PAD_WEST': ((16, 2), (16, 2)), 'PAD_NORTH': ((16, 3), (16, 3))},
}
NAMES = {'XBOX': 'xbox', 'PLAYSTATION': 'playstation'}


def cut(sheet, cell):
    row, col = cell
    return sheet.crop((col * CELL, row * CELL, (col + 1) * CELL, (row + 1) * CELL))


def luma(p):
    return (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000


def symbol_color(dark_tile):
    """The most saturated opaque pixel of a Dark-sheet face tile: the symbol's own color."""
    best = max((p for p in dark_tile.getdata() if p[3] == 255), key=lambda p: max(p[:3]) - min(p[:3]))
    return best[:3]


def recolor(light_tile, color):
    """Paint the dark symbol of a Light-sheet face tile in `color`; the white disc and the alpha stay as they are."""
    pixels = list(light_tile.getdata())
    ink = min(luma(p) for p in pixels if p[3] == 255)
    out = []
    for r, g, b, a in pixels:
        t = min(1.0, max(0.0, (255 - luma((r, g, b))) / (255 - ink)))     # 0 on the white disc, 1 on the symbol
        out.append(tuple(round(255 + t * (c - 255)) for c in color) + (a,))
    result = Image.new('RGBA', light_tile.size)
    result.putdata(out)
    return result


WHITE, DIM = (255, 255, 255), (74, 84, 98)   # the lit and the pressed arm of a Light-sheet D-pad
LIT_INPUTS = {'PAD_DPAD_UP', 'PAD_DPAD_DOWN', 'PAD_DPAD_LEFT', 'PAD_DPAD_RIGHT', 'GLYPH_IN_DPAD_V', 'GLYPH_IN_DPAD_H'}


def swap_lit(tile):
    """Exchange the white and the dim arms; pixels that are neither (outlines, creases) and the alpha stay as they are."""
    out = []
    for r, g, b, a in tile.getdata():
        t = (r - DIM[0]) / (WHITE[0] - DIM[0])
        on_axis = a and all(abs(c - round(d + t * (w - d))) <= 10 for c, d, w in zip((r, g, b), DIM, WHITE)) and -0.05 <= t <= 1.05
        if on_axis:
            t = 1 - min(1.0, max(0.0, t))
            r, g, b = (round(d + t * (w - d)) for d, w in zip(DIM, WHITE))
        out.append((r, g, b, a))
    result = Image.new('RGBA', tile.size)
    result.putdata(out)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zip', type=Path, required=True, help='audune-prompts-1.3.1.zip')
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    styles = root / 'assets/appendage-glyph/styles'
    dst = root / 'assets/appendage-glyph/audune'
    dst.mkdir(parents=True, exist_ok=True)
    raw = a.zip.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PACKAGE_SHA256, 'unexpected Audune package'
    pack = zipfile.ZipFile(io.BytesIO(raw))
    sheets, sheet_hash = {}, {}
    for name in (LIGHT, DARK):
        data = pack.read(name)
        sheet_hash[name] = hashlib.sha256(data).hexdigest()
        sheets[name] = Image.open(io.BytesIO(data)).convert('RGBA')
        assert sheets[name].size == (768, 1408)
    manifest = json.loads((styles / 'provenance.json').read_text())

    def make(style, input_, appearance, cell, recolor_from=None):
        stem = f'audune_{NAMES[style]}_{input_.lower().removeprefix("glyph_in_").removeprefix("pad_")}'
        stem += '_colored' if appearance == 'COLORED' else ''
        tile = cut(sheets[LIGHT], cell)
        change = 'cell cut from the sheet'
        if input_ in LIT_INPUTS:
            tile = swap_lit(tile)
            change += '; white and dim arms swapped so the pressed directions are bright'
        if recolor_from:
            tile = recolor(tile, symbol_color(cut(sheets[DARK], cell)))
            change = 'cell cut from the sheet; dark symbol recolored to the symbol color of the Dark sheet'
        assert tile.size == (CELL, CELL) and tile.getchannel('A').getbbox(), stem
        tile.save(dst / f'{stem}.png')
        entry = {'style': style, 'input': input_}
        if appearance == 'COLORED':
            entry['appearance'] = 'COLORED'
        entry.update({'pack': 'audune', 'sheet': LIGHT, 'cell': list(cell), 'change': change,
                      'file': f'{stem}.png', 'sha256': hashlib.sha256((dst / f'{stem}.png').read_bytes()).hexdigest()})
        if recolor_from:
            entry['color_sheet'] = DARK
        return entry

    out, used = [], set()
    for asset in manifest['assets']:
        style = asset['style']
        if style not in CELLS:
            out.append(asset)
            continue
        input_, appearance = asset['input'], asset.get('appearance', 'STANDARD')
        if input_ in FACES[style]:
            standard, colored = FACES[style][input_]
            if appearance == 'COLORED':
                entry = make(style, input_, appearance, colored, recolor_from=(style == 'PLAYSTATION'))
            else:
                entry = make(style, input_, appearance, standard)
        else:
            assert appearance == 'STANDARD', asset
            entry = make(style, input_, appearance, CELLS[style][input_])
        out.append(entry)
        used.add(entry['file'])
    for stale in dst.glob('audune_*.png'):
        if stale.name not in used:
            stale.unlink()
    manifest['audune'] = {
        'name': 'Audune Prompts', 'version': '1.3.1', 'license': 'CC-BY-SA-4.0', 'author': 'Audune',
        'source': 'https://audune.itch.io/audune-prompts', 'license_url': 'https://creativecommons.org/licenses/by-sa/4.0/',
        'package_sha256': PACKAGE_SHA256, 'sheet_sha256': sheet_hash, 'notice': 'assets/appendage-glyph/audune/NOTICE.txt',
        'folder': 'assets/appendage-glyph/audune'}
    manifest['assets'] = out
    (styles / 'provenance.json').write_text(json.dumps(manifest, indent=2) + '\n')
    # Kenney files that no Kenney entry refers to any more stay out of the tree.
    kept = {x['file'] for x in out if x.get('pack') != 'audune'}
    for path in sorted(styles.glob('*.png')):
        if path.name not in kept:
            path.unlink()
    print(len(out), 'tiles in the manifest,', len(used), 'from Audune Prompts')


if __name__ == '__main__':
    main()
