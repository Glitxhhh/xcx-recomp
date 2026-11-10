"""One opt-in Online selection insertion for the inspected XCX DE 2.0.0 image."""
import hashlib

BUILD_ID = 'ec4ed46bbc4a9ebbe7ce0af7a4a8ebbc09e5724a'
NSO_SHA256 = '0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026'
BASE = 0x7000000000
ENTRY, SITE, HANDLED = BASE + 0x83aa70, BASE + 0x83abcc, BASE + 0x83aac0
BUILD_LABEL=BASE+0x83a46c
INSTALL=BASE+0x83938c
LABEL_ENTRY=BASE+0x66bee8
LABEL_TAIL=BASE+0x66bf44
RAW_SETTER=BASE+0x91720c
LIFETIME = {BASE + 0x8392a0, BASE + 0x8394a0}
WORDS = {0x83a46c:0x97f8c69f,0x83938c:0x52823309,0x839340:0x91026260,0x66bee8:0xa9bd7bfd,0x66bf44:0x140aac87,0x91720c:0xa9bd7bfd,0x83abbc: 0xf948ce61, 0x83abc0: 0xaa1303e0, 0x83abc4: 0x94000375,
         0x83abc8: 0x3607f7c0, 0x83abcc: 0xb000a8e8, 0x83aac0: 0x52800034,
         0x83aac4: 0x2a1403e0, 0x83aad0: 0xa8c37bfd, 0x83aad4: 0xd65f03c0,
         0x838fcc: 0x71000c3f}


def validate(mod, raw):
    if mod.name != 'main' or mod.base != BASE or mod.nso.build_id != BUILD_ID:
        raise ValueError('Mod menu: unsupported module, layout or build ID')
    if hashlib.sha256(raw).hexdigest() != NSO_SHA256:
        raise ValueError('Mod menu: unsupported main NSO hash')
    for off, word in WORDS.items():
        if mod.word(BASE + off) != word:
            raise ValueError(f'Mod menu: original instruction mismatch at main+{off:#x}')


def validate_patches(table):
    for off in table.sites.get('main', {}):
        if 0x83aa70 <= off < 0x83ad20 or 0x8392a0 <= off < 0x83959c or 0x83a180 <= off < 0x83a540 or 0x66bee8 <= off < 0x66bf80 or 0x91720c <= off < 0x917288:
            raise ValueError('Mod menu: overlapping instruction patch')
