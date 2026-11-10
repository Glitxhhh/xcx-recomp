"""Verified XCX 2.0.0 sites for experimental Appendage glyph styles."""

BUILD = 'ec4ed46bbc4a9ebbe7ce0af7a4a8ebbc09e5724a'
SHA256 = '0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026'
WORDS = {0x60f68c: 0x940c224d, 0xa27678: 0x9400802a,
         0x60f6a8: 0x940c2246, 0x60f6c4: 0x940c223f,
         0xa2fac0: 0xa9bd7bfd, 0xa2fcd0: 0xd10203ff,
         0x5a665c: 0x9400d0c6, 0x5c6ed4: 0x94004ea8, 0x55ff1c: 0x9412f42b,
         0x5da974: 0x2a0103e2, 0x5da984: 0x17fe154f,
         0x55fec0: 0xa9bd7bfd, 0x55fecc: 0x910003fd,
         0xa21afc: 0xf9400c0a, 0xa325f8: 0x9400544a, 0xa32688: 0x94005426}
# Opt-in icon probe sites, checked against the pinned main by build/.../verify_sites.py: 0xa21afc is the ML:icon layout
# method (layouter vtable address point 0x1c93f08, slot +0x80); the two BLs in f_7000a322cc bind entry 0 of the icon
# source's texture set (node+0x3b8) through the shared bind helper 0xa47720.
ICON_SOURCE_BINDS = (0xa325f8, 0xa32688)


def verified(mod):
    if mod.name != 'main' or mod.nso.build_id != BUILD:
        return False
    if mod.nso.source_sha256 != SHA256 or mod.base != 0x7000000000:
        raise ValueError('Appendage prototype: build identity mismatch')
    for offset, word in WORDS.items():
        if mod.word(mod.base + offset) != word:
            raise ValueError(f'Appendage prototype: instruction mismatch at {offset:#x}')
    return True


def entry(mod, address):
    offset = address - mod.base
    if offset in (0xa2fac0, 0xa2fcd0):
        return 'appendage_glyph_retire(c->x[0]);'
    if offset == 0xa21afc:
        return 'appendage_glyph_icon_layout(c);'
    return ''


def call(mod, address, target):
    offset = address - mod.base
    if offset in (0x60f68c, 0x60f6a8, 0x60f6c4):
        assert target == mod.base + 0x917fc0
        return f'appendage_glyph_attach(c); f_{target:x}(c);'
    if offset == 0x55ff1c:
        assert target == mod.base + 0xa1cfc8
        return f'appendage_glyph_lookup_attach(c, c->x[29]); f_{target:x}(c);'
    if offset == 0xa27678:
        assert target == mod.base + 0xa47720
        return f'appendage_glyph_bind(c, c->x[19], f_{target:x});'
    if offset in ICON_SOURCE_BINDS:
        assert target == mod.base + 0xa47720
        return f'appendage_glyph_icon_bind(c, f_{target:x});'
    return f'f_{target:x}(c);'
