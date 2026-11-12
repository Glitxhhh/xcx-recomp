"""Tests of the mod manager (installer/mods.py): zip layouts, the on/off lists and the download check.

    python tests/mods_manager_test.py
"""
import hashlib
import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from installer import mods  # noqa: E402


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
