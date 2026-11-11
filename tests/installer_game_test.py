"""--game: a folder of game files is sorted by content. The NSPs here are synthetic: a PFS0 holding one NCA whose
header is encrypted with a random header_key and says only its content type and title id. No game data.

Run: python tests/installer_game_test.py   (needs the installer's packages: tools/requirements-installer.txt)
"""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from installer import game_files  # noqa: E402
from installer.runner import InstallError  # noqa: E402

HEADER_KEY = os.urandom(64)
PROGRAM, META = 0, 1
BASE_TITLE = 0x0100453019AA8000


def nca(content_type, title_id, bktr=False):
    header = bytearray(0xC00)
    header[0x200:0x204] = b'NCA3'
    header[0x205] = content_type
    struct.pack_into('<Q', header, 0x208, len(header))
    struct.pack_into('<Q', header, 0x210, title_id)
    if bktr:
        header[0x400 + 4] = 4   # the first section's FS header says BKTR
    encrypted = b''
    for i in range(6):
        cipher = Cipher(algorithms.AES(HEADER_KEY), modes.XTS(i.to_bytes(16, 'big'))).encryptor()
        encrypted += cipher.update(bytes(header[i * 512:(i + 1) * 512])) + cipher.finalize()
    return encrypted


def nsp(*ncas):
    """A PFS0 holding the NCAs as 0.nca, 1.nca, ..."""
    names = [f'{i}.nca\0'.encode() for i in range(len(ncas))]
    strings = b''.join(names)
    table, offset, name_offset = b'', 0, 0
    for data, name in zip(ncas, names):
        table += struct.pack('<QQII', offset, len(data), name_offset, 0)
        offset += len(data)
        name_offset += len(name)
    header = b'PFS0' + struct.pack('<III', len(ncas), len(strings), 0)
    return header + table + strings + b''.join(ncas)


class GameFilesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.keys = self.dir / 'prod.keys'
        self.keys.write_text(f'header_key = {HEADER_KEY.hex()}\n')
        self.folder = self.dir / 'game'
        self.folder.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def add(self, name, data):
        path = self.folder / name
        path.write_bytes(data)
        return path

    def options(self, **given):
        return SimpleNamespace(game=self.folder, keys=self.keys, base=None, update=None, dlc=None, **given)

    def test_a_program_nca_is_the_base_game(self):
        path = self.add('x.nsp', nsp(nca(PROGRAM, BASE_TITLE), nca(META, BASE_TITLE)))
        self.assertEqual(game_files.kind_of(path, {'header_key': HEADER_KEY}), 'base')

    def test_a_bktr_program_nca_is_the_update(self):
        path = self.add('x.nsp', nsp(nca(PROGRAM, BASE_TITLE + 0x800, bktr=True)))
        self.assertEqual(game_files.kind_of(path, {'header_key': HEADER_KEY}), 'update')

    def test_only_a_meta_nca_is_add_on_content(self):
        path = self.add('x.nsp', nsp(nca(META, BASE_TITLE + 0x1001)))
        self.assertEqual(game_files.kind_of(path, {'header_key': HEADER_KEY}), 'dlc')

    def test_a_file_that_is_no_game_file_is_none(self):
        path = self.add('x.nsp', b'not a game')
        self.assertIsNone(game_files.kind_of(path, {'header_key': HEADER_KEY}))

    def test_the_folder_is_sorted_by_content_not_by_name(self):
        base = self.add('update.nsp', nsp(nca(PROGRAM, BASE_TITLE)))
        update = self.add('base.nsp', nsp(nca(PROGRAM, BASE_TITLE + 0x800, bktr=True)))
        dlc = self.add('dlc.nsp', nsp(nca(META, BASE_TITLE + 0x1001)))
        self.add('notes.txt', b'ignored')
        options = self.options()
        game_files.resolve(options)
        self.assertEqual((options.base, options.update, options.dlc), (base, update, [dlc]))

    def test_a_file_given_with_base_wins(self):
        self.add('a.nsp', nsp(nca(PROGRAM, BASE_TITLE)))
        self.add('b.nsp', nsp(nca(PROGRAM, BASE_TITLE + 0x800, bktr=True)))
        mine = self.dir / 'mine.nsp'
        options = self.options()
        options.base = mine
        game_files.resolve(options)
        self.assertEqual(options.base, mine)

    def test_a_folder_without_an_update_says_so(self):
        self.add('a.nsp', nsp(nca(PROGRAM, BASE_TITLE)))
        with self.assertRaises(InstallError) as caught:
            game_files.resolve(self.options())
        self.assertEqual(caught.exception.id, 'check.game_no_update')

    def test_a_folder_without_a_base_game_says_so(self):
        with self.assertRaises(InstallError) as caught:
            game_files.resolve(self.options())
        self.assertEqual(caught.exception.id, 'check.game_no_base')

    def test_a_missing_path_says_so(self):
        options = self.options()
        options.game = self.dir / 'nowhere'
        with self.assertRaises(InstallError) as caught:
            game_files.resolve(options)
        self.assertEqual(caught.exception.id, 'check.input_missing')


if __name__ == '__main__':
    unittest.main()
