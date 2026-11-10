"""Check the actual release copy and split-build paths without game inputs or downloads."""
from pathlib import Path
from unittest.mock import patch
import importlib.util
import sys
import tempfile
import unittest
import re
from types import SimpleNamespace
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import build_game as bg
import make_release as release

class ModPackaging(unittest.TestCase):
    def test_release_contains_public_guides_example_and_lua_license(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            release.build_sources(out)
            for name in ['licenses/lua-LICENSE.txt', 'docs/mods/README.md',
                         'docs/mods/02-first-mod.md', 'docs/lua-mods.md',
                         'docs/native-providers.md', 'docs/provider-actions.md',
                         'examples/mods/hello/main.lua', 'examples/mods/hello/mod.ini']:
                self.assertEqual((out / name).read_bytes(), (ROOT / ('deps/lua/LICENSE.txt' if name.startswith('licenses/') else name)).read_bytes())
            for name in ['tests', 'docs/review', 'deps/lua/pristine', 'build', 'mods', 'mod-settings']:
                self.assertFalse((out / name).exists(), name)

    def test_packaged_link_needs_no_lua_sources_or_preparation_tool(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            release.build_sources(out)
            with patch.object(release, 'RUNTIME_LIBS', ()):
                release.runtime_lib(out, out / 'llvm')
            spec = importlib.util.spec_from_file_location('packaged_build_game', out / 'tools/build_game.py')
            packaged = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(packaged)
            packaged.RUNTIME = out / 'runtime'
            packaged.vma_include = lambda: out / 'include'
            # Fail even if the development checkout happens to make this module importable.
            with patch.dict(sys.modules, {'prepare_lua': None}):
                text, _, _ = packaged.ninja_file(out / 'gen', out / 'link', {}, 2, runtime_lib=out / 'runtime/libxcxrt.a')
            self.assertNotIn('/lua/', text)
            self.assertIn('mod_menu_adapter.c', text)

    def test_runtime_archive_defers_adapter_to_install(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(bg, 'vma_include', return_value=Path('include')):
            text, _, _ = bg.ninja_file(ROOT / 'tools/runtime-layout', Path(tmp), {}, 2, make_lib=True)
            self.assertNotIn('mod_menu_adapter.c', text)
            self.assertIn('mod_runtime.c', text)
            self.assertIn('lapi.c', text)

    def test_installer_requests_checked_menu_generation(self):
        import installer.steps as steps
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            ctx = SimpleNamespace(dest=root / 'install', temp=root / 'temp', state={'version': '2.0.0'},
                                  keys={'lift': 'test', 'extract': 'inputs'}, layout=SimpleNamespace(recomp=ROOT / 'recomp'),
                                  reporter=SimpleNamespace(progress=lambda *a, **k: None))
            self.assertNotEqual(steps.Lift().key(ctx), steps.fingerprint('lift-2', 'inputs', ctx.layout.recomp))
            seen = []
            def tool(ctx, name, script, args, parse):
                seen.extend(args)
                (ctx.temp / 'gen').mkdir(parents=True)
                return 0, root / 'tool.log'
            with patch.object(steps, 'run_tool', side_effect=tool):
                steps.Lift().run(ctx)
            self.assertIn('--mod-menu-hook', seen)

    def test_installer_copies_only_managed_public_files(self):
        import installer.steps as steps
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); package = root / 'setup'; package.mkdir()
            release.build_sources(package)
            xlate = package / 'bin/xcx_xlate'; xlate.parent.mkdir(); xlate.write_bytes(b'test translator')
            dest = root / 'install'; dest.mkdir()
            protected = {}
            for name in ['mods/hello/main.lua', 'mods/enabled.txt', 'mod-settings/hello.ini', 'profile/sentinel', 'xcx.ini']:
                p = dest / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text('keep ' + name); protected[name] = p.read_bytes()
            layout = SimpleNamespace(root=package, dev=False, xlate=xlate)
            ctx = SimpleNamespace(layout=layout, dest=dest, keys={'compile': 'test'}, reporter=SimpleNamespace(progress=lambda *a, **k: None))
            prepare = steps.Prepare(); key = prepare.key(ctx); record = prepare.run(ctx)
            self.assertTrue(prepare.current(ctx, record))
            self.assertEqual((dest / 'licenses/lua-LICENSE.txt').read_bytes(), (ROOT / 'deps/lua/LICENSE.txt').read_bytes())
            self.assertTrue((dest / 'examples/mods/hello/main.lua').is_file())
            for doc in [*(dest / 'docs/mods').glob('*.md'), *(dest / 'docs').glob('*.md')]:
                for link in re.findall(r'\]\(([^)]+)\)', doc.read_text()):
                    if '://' not in link:
                        self.assertTrue((doc.parent / link.split('#')[0]).exists(), (doc, link))
            for name, data in protected.items(): self.assertEqual((dest / name).read_bytes(), data)
            guide = package / 'docs/mods/README.md'; guide.write_text(guide.read_text() + '\nUpdated guide.\n')
            self.assertNotEqual(key, prepare.key(ctx)); self.assertFalse(prepare.current(ctx, record))
            prepare.run(ctx); self.assertTrue(prepare.current(ctx, record))

if __name__ == '__main__':
    unittest.main()
