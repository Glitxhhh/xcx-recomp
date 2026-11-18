"""Tests of the mod manager (installer/mods.py): zip layouts, the on/off lists and the download check.

    python tests/mods_manager_test.py
"""
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from installer import mods  # noqa: E402


HAND_PLACED = ('Unburger Patch/romfs/bdat/a.bdat',
               'Uncensor/atmosphere/contents/0100453019AA8000/romfs/chr/b.wimdo',
               'Uncensor/atmosphere/contents/0100453019AA8000/exefs/main.npdm',
               'atmosphere/contents/0100453019AA8000/romfs/c.bin',
               '0100453019AA8000/romfs/d.bin',
               'romfs/e.bin',
               'Loader Mod/Loader Mod/romfs/mod/f.bin',
               'good/romfs/g.bin')
NOT_MODS = ('Readme Only/readme.txt', 'Two/a/romfs/x', 'Two/b/romfs/y', 'Other/0100000000010000/romfs/z',
            'Lua Mod/mod.ini', 'mod.zip', '.install-x/romfs/q')


def clang():
    found = shutil.which('clang') or ('C:/msys64/ucrt64/bin/clang.exe' if os.name == 'nt' else None)
    return found if found and Path(found).is_file() else None


def make_zip(path, names):
    with zipfile.ZipFile(path, 'w') as z:
        for n in names:
            z.writestr(n, b'data ' + n.encode())


class ModsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dest = Path(self.tmp.name) / 'XCX'
        self.dest.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def zip(self, names):
        path = Path(self.tmp.name) / 'm.zip'
        make_zip(path, names)
        return path

    def files(self, mid):
        root = self.dest / 'mods' / mid / 'romfs'
        return sorted(p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file())

    def test_layouts(self):
        # romfs/<path>, romfs/mod/<path> (the XCXDE mod loader) and a top folder; exefs is left out
        mods.add_zip(self.dest, self.zip(['romfs/chr/np/a.wimdo', 'exefs/subsdk9']), 'plain')
        self.assertEqual(self.files('plain'), ['chr/np/a.wimdo'])
        mods.add_zip(self.dest, self.zip(['My Mod/romfs/mod/bdat/common.bdat', 'My Mod/readme.txt']), 'loader')
        self.assertEqual(self.files('loader'), ['bdat/common.bdat'])

    def test_rejects(self):
        for names, error in ((['readme.txt'], 'mods.zip_no_romfs'), (['../romfs/x'], 'mods.zip_unsafe'),
                             (['romfs/A.bin', 'romfs/a.bin'], 'mods.zip_duplicate')):
            with self.assertRaises(mods.ModError) as e:
                mods.add_zip(self.dest, self.zip(names), 'bad')
            self.assertEqual(e.exception.id, error)
            self.assertFalse((self.dest / 'mods' / 'bad').exists())

    def test_info_for_the_game(self):
        # <id>/library.ini, shared with the game's F1 menu (runtime/mod_library.c)
        mods.add_zip(self.dest, self.zip(['romfs/x.bin']), 'named', name='Potato\nTatsu')
        info = mods.read_info(self.dest / 'mods' / 'named')
        self.assertEqual((info['name'], info['kind'], info['source']), ('Potato Tatsu', 'romfs', 'zip'))
        self.assertEqual(self.files('named'), ['x.bin'])
        # a version the game wrote wins over the manager's record
        (self.dest / 'mods' / 'named' / 'library.ini').write_text('name = Tatsu\nversion = 2\nkind = romfs\n')
        self.assertEqual([(m['name'], m['version']) for m in mods.installed(self.dest)], [('Tatsu', '2')])

    def test_ids_from_file_names(self):
        path = Path(self.tmp.name) / 'Tatsu Potato v1.zip'
        make_zip(path, ['romfs/x.bin'])
        self.assertEqual(mods.add_zip(self.dest, path), 'tatsu-potato-v1')

    def test_lists(self):
        for mid in ('one', 'two'):
            mods.add_zip(self.dest, self.zip(['romfs/x.bin']), mid)
        lst = self.dest / 'mods' / 'romfs-enabled.txt'
        mods.set_enabled(self.dest, 'one', True)
        mods.set_enabled(self.dest, 'two', True)
        self.assertEqual(lst.read_text(), 'two\none\n')   # the one turned on last wins
        mods.set_enabled(self.dest, 'two', False)
        self.assertEqual(lst.read_text(), 'one\n')
        mods.remove(self.dest, 'one')
        self.assertEqual(lst.read_text(), '')
        self.assertEqual([m['id'] for m in mods.installed(self.dest)], ['two'])

    def put(self, *paths):
        for rel in paths:
            p = self.dest / 'mods' / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b'x')

    def test_hand_placed_layouts(self):
        # #125: folders unpacked into mods/ as they come are moved to mods/<id>/romfs
        self.put(*HAND_PLACED)
        notes = []
        have = {m['id']: m for m in mods.installed(self.dest, notes)}
        self.assertEqual(sorted(have), ['good', 'loader-mod', 'romfs-mod', 'romfs-mod-2', 'romfs-mod-3',
                                        'unburger-patch', 'uncensor'])
        self.assertEqual(self.files('unburger-patch'), ['bdat/a.bdat'])
        self.assertEqual(self.files('uncensor'), ['chr/b.wimdo'])
        self.assertEqual(self.files('loader-mod'), ['f.bin'])
        self.assertEqual(sorted(f for m in ('romfs-mod', 'romfs-mod-2', 'romfs-mod-3') for f in self.files(m)),
                         ['c.bin', 'd.bin', 'e.bin'])
        self.assertEqual(have['unburger-patch']['name'], 'Unburger Patch')
        self.assertFalse(have['unburger-patch']['enabled'])
        self.assertEqual(sorted(p['folder'] for i, p in notes if i == 'mods.moved'),
                         ['0100453019AA8000', 'Loader Mod', 'Unburger Patch', 'Uncensor', 'atmosphere', 'romfs'])
        # the next listing finds nothing to move, and the moved mod switches like any other
        again = []
        mods.installed(self.dest, again)
        self.assertEqual(again, [])
        mods.set_enabled(self.dest, 'unburger-patch', True)
        self.assertEqual((self.dest / 'mods' / 'romfs-enabled.txt').read_text(), 'unburger-patch\n')

    def test_hand_placed_skips(self):
        self.put(*NOT_MODS)
        notes = []
        self.assertEqual(mods.installed(self.dest, notes), [])
        self.assertEqual(sorted((i, p['folder']) for i, p in notes),
                         [('mods.skip_lua_name', 'Lua Mod'), ('mods.skip_no_romfs', 'Readme Only'),
                          ('mods.skip_other_game', 'Other'), ('mods.skip_two_romfs', 'Two'), ('mods.skip_zip', 'mod.zip')])
        self.assertTrue((self.dest / 'mods' / 'Two' / 'a' / 'romfs' / 'x').is_file())   # left as it was

    @unittest.skipUnless(clang(), 'needs clang (MSYS2 UCRT64)')
    def test_game_reads_the_folder_the_same_way(self):
        # runtime/mod_library.c, normalise, on one copy and normalise here on another: the same files end up in the
        # same places, the same folders are named, and romfs_mods.c finds a moved mod's file once it is on
        exe = Path(self.tmp.name) / 'mod_library_scan.exe'
        plat = 'win32' if os.name == 'nt' else 'linux'
        libs = ['-lwinmm', '-lbcrypt'] if os.name == 'nt' else ['-lpthread', '-lm']
        rt = ROOT / 'runtime'
        subprocess.run([clang(), '-O1', '-w', f'-I{rt}', f'-I{rt / "platform"}', str(ROOT / 'tests/mod_library_scan.c'),
                        str(rt / 'romfs_mods.c'), str(rt / f'platform/{plat}.c'), str(rt / 'platform/common.c'),
                        str(rt / 'sha256.c'), *libs, '-o', str(exe)], check=True)
        self.put(*HAND_PLACED, *NOT_MODS)
        game = Path(self.tmp.name) / 'game'
        shutil.copytree(self.dest / 'mods', game)
        env = dict(os.environ, XCX_MOD_DIR=str(game))
        out = subprocess.run([str(exe), 'unburger-patch', 'rom:/bdat/a.bdat'], env=env, capture_output=True,
                             text=True, encoding='utf-8', check=True).stdout.splitlines()
        notes = []
        rows = mods.installed(self.dest, notes)
        listing = lambda root: sorted(p.relative_to(root).as_posix() for p in root.rglob('*')
                                      if p.is_file() and p.name != 'romfs-enabled.txt')
        self.assertEqual(listing(game), listing(self.dest / 'mods'))
        self.assertEqual(sorted(line.split(' ', 3)[1:3] for line in out if line.startswith('row ')),
                         sorted([m['id'], m['kind']] for m in rows))
        game_notes = [line.split(' ', 2) for line in out if line.startswith('note ')]
        self.assertEqual(len(game_notes), len(notes))
        for (_, error, text), (i, params) in zip(game_notes, notes):
            self.assertIn(params['folder'], text)
            self.assertEqual(error == '1', i != 'mods.moved')
        lookup = next(line for line in out if line.startswith('lookup ')).split(' ', 2)
        self.assertEqual(lookup[1], '1')
        self.assertTrue(lookup[2].replace(chr(92), '/').endswith('/unburger-patch/romfs/bdat/a.bdat'))

    def test_zip_for_another_game(self):
        with self.assertRaises(mods.ModError) as e:
            mods.add_zip(self.dest, self.zip(['atmosphere/contents/0100000000010000/romfs/x.bin']), 'other')
        self.assertEqual(e.exception.id, 'mods.zip_other_game')
        mods.add_zip(self.dest, self.zip(['atmosphere/contents/0100453019AA8000/romfs/x.bin']), 'ours')
        self.assertEqual(self.files('ours'), ['x.bin'])

    def test_slug(self):
        for text, mid in (('Unburger Patch', 'unburger-patch'), ('  Tatsu -- Potato v1.2 ', 'tatsu-potato-v1.2'),
                          ('2.0 fix', 'mod-2.0-fix'), ('Мод', 'mod'), ('x' * 80, 'x' * 64)):
            self.assertEqual(mods.slug(text), mid)
            self.assertTrue(mods.valid_id(mods.slug(text)))

    def test_download_check(self):
        data = b'x' * 1000
        real = mods.opener
        mods.opener = lambda url: io.BytesIO(data)
        try:
            out = Path(self.tmp.name) / 'd.bin'
            mods.download('https://example.invalid/a', 1000, hashlib.sha256(data).hexdigest(), out)
            for size, sha in ((1000, '0' * 64), (999, hashlib.sha256(data).hexdigest())):
                with self.assertRaises(mods.ModError):
                    mods.download('https://example.invalid/a', size, sha, out)
        finally:
            mods.opener = real
        with self.assertRaises(mods.ModError):
            mods.opener('http://example.invalid/a')


if __name__ == '__main__':
    unittest.main()
