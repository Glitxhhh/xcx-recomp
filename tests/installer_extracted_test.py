"""installer/extracted.py: finding an extracted game folder, checking loose RomFS files against a table and packing them
into an image whose index.json the RomFS check accepts. Synthetic files, no game data."""
import hashlib
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools'), str(ROOT / 'tools' / 'vendor' / 'macnativegames')]

import extract  # noqa: E402
from installer import extracted  # noqa: E402
from installer.runner import InstallError  # noqa: E402
from romfs_files import path_key  # noqa: E402


class ExtractedTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.game = self.dir / 'game'
        (self.game / 'exefs').mkdir(parents=True)
        (self.game / 'exefs' / 'main').write_bytes(b'main')
        self.files = {'a/b.dat': os.urandom(1000), 'c.bin': os.urandom(33), 'd/e/f.x': b''}
        for path, data in self.files.items():
            p = self.game / 'romfs' / path
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        self.table = {'version': '9.9.9', 'romfs_sha256': 'ab' * 32,
                      'files': {path_key(p): [len(d), hashlib.sha256(d).hexdigest()] for p, d in self.files.items()}}

    def test_find(self):
        self.assertEqual(set(extracted.find(self.game)), {'exefs', 'loose'})
        (self.game / 'romfs.bin').write_bytes(b'x')
        self.assertEqual(extracted.find(self.game)['image'], self.game / 'romfs.bin')
        self.assertIsNone(extracted.find(self.dir))

    def test_loose_files_pack_into_an_accepted_image(self):
        (self.game / 'romfs' / 'emulator-note.txt').write_text('not in the table')
        files = extracted.check_loose(self.game / 'romfs', self.table)
        self.assertEqual([p for p, _, _ in files], sorted(self.files, key=str.encode))
        out = self.dir / 'install' / 'romfs'
        report = extracted.pack_loose(files, self.table, out)
        image = (out / 'romfs.bin').read_bytes()
        for e in report['files']:
            self.assertEqual(e['offset'] % extracted.ALIGN, 0)
            self.assertEqual(image[e['offset']:e['offset'] + e['size']], self.files[e['path']])
        self.assertEqual(report['romfs_sha256'], hashlib.sha256(image).hexdigest())
        expected = {'romfs': {'sha256': self.table['romfs_sha256'], 'size': 1 << 34}}
        self.assertTrue(extract.romfs_verified(out, expected))
        self.assertFalse(extract.romfs_verified(out, {'romfs': {'sha256': 'cd' * 32, 'size': 1}}))

    def test_wrong_or_missing_files_are_refused(self):
        (self.game / 'romfs' / 'c.bin').write_bytes(os.urandom(33))
        (self.game / 'romfs' / 'a' / 'b.dat').unlink()
        with self.assertRaises(InstallError) as e:
            extracted.check_loose(self.game / 'romfs', self.table)
        self.assertEqual(e.exception.id, 'check.romfs_files_wrong')
        self.assertEqual((e.exception.params['bad'], e.exception.params['missing']), (1, 1))

    def test_image_is_linked_and_indexed(self):
        src = self.dir / 'romfs.bin'
        src.write_bytes(os.urandom(4096))
        report = extracted.take_image(src, self.dir / 'out', lambda image: [{'path': 'x', 'offset': 0, 'size': 1}])
        self.assertEqual(report['romfs_sha256'], hashlib.sha256(src.read_bytes()).hexdigest())
        self.assertEqual(json.loads((self.dir / 'out' / 'index.json').read_text())['files'][0]['path'], 'x')


if __name__ == '__main__':
    unittest.main()
