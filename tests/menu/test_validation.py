"""Validation rejection tests; no game input needed."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'recomp'))
import mod_menu as h

class Validation(unittest.TestCase):
    def module(self):
        return SimpleNamespace(name='main', base=h.BASE, nso=SimpleNamespace(build_id=h.BUILD_ID),
                               word=lambda a: h.WORDS[a - h.BASE])
    def test_wrong_build_and_layout(self):
        for field, value in [('name', 'sdk'), ('base', 0)]:
            m = self.module(); setattr(m, field, value)
            with self.assertRaises(ValueError): h.validate(m, b'')
        m = self.module(); m.nso.build_id = 'unsupported'
        with self.assertRaises(ValueError): h.validate(m, b'')
    def test_overlapping_patch(self):
        h.validate_patches(SimpleNamespace(sites={'main': {0x1234: None}}))
        for off in [0x83abc8, 0x83abcc, 0x8392a0, 0x8394a0,0x66bf44,0x91720c,0x83a46c]:
            with self.assertRaises(ValueError):
                h.validate_patches(SimpleNamespace(sites={'main': {off: None}}))
    def test_wrong_hash(self):
        with self.assertRaises(ValueError): h.validate(self.module(), b'unsupported')
    def test_every_original_instruction(self):
        with patch.object(h.hashlib, 'sha256', return_value=SimpleNamespace(hexdigest=lambda: h.NSO_SHA256)):
            h.validate(self.module(), b'fixture')
            for off in h.WORDS:
                m = self.module()
                m.word = lambda a, off=off: h.WORDS[a-h.BASE] ^ (1 if a-h.BASE == off else 0)
                with self.assertRaises(ValueError): h.validate(m, b'fixture')

if __name__ == '__main__': unittest.main()
