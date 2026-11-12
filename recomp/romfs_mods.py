"""Loose RomFS mod files in place of sts.ard entries, for the verified XCX DE 2.0.0 main.

The game's archive device (sts.arh/sts.ard, vtable at main+0x1d0f480) keeps a list of paths it declines, so that
those load as loose rom:/<path> files instead. Its three path methods walk that list, a run of NUL-terminated
case-insensitive path prefixes ending in an empty string, read from [[main+0x1d6ee00]]+8:
  main+0x13b2ea0  read a whole file (vtable +0x40), path at x1+0x94, list loaded at main+0x13b2ee8
  main+0x13b3230  look a file up (vtable +0x48), path in x1, list loaded at main+0x13b3260
  main+0x13b1460  open a streamed read (another vtable, main+0x1d0fd50), path at [x0+0x10]+0x94, list at main+0x13b1498
After each load of the list pointer the generated C passes it through runtime/romfs_mods.c, which returns a list
holding just the path when an enabled mod has a file for it. The archive then declines the path, the game opens
rom:/<path>, and runtime/fs_host.c serves the mod's copy. The lookup happens at every operation, so mods can change
while the game runs.
"""

BUILD = 'ec4ed46bbc4a9ebbe7ce0af7a4a8ebbc09e5724a'
SHA256 = '0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026'
LIST_LOAD = 0xf9400508   # ldr x8, [x8, #8]
# offset of the list load: register holding the path string
LISTS = {0x13b2ee8: 23,   # x23 = x1 + 0x94, set at main+0x13b2ee4
         0x13b3260: 20,   # x20 = x1, set at main+0x13b3254
         0x13b1498: 20}   # x20 = [x0+0x10] + 0x94, set at main+0x13b1490


def verified(mod):
    if mod.name != 'main' or mod.nso.build_id != BUILD:
        return False
    if mod.nso.source_sha256 != SHA256 or mod.base != 0x7000000000:
        raise ValueError('RomFS mods: build identity mismatch')
    for offset in LISTS:
        if mod.word(mod.base + offset) != LIST_LOAD:
            raise ValueError(f'RomFS mods: instruction mismatch at main+{offset:#x}')
    return True


def after(mod, address):
    """C to run after the instruction at address, or None."""
    path = LISTS.get(address - mod.base)
    if path is None:
        return None
    return f'c->x[8] = romfs_mod_declines(c->x[8], c->x[{path}]);'
