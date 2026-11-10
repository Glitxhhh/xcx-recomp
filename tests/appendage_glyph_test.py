"""Linux prototype checks; --gpu-build additionally links/tests an existing isolated candidate's objects."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(command, env=None, log=None):
    result = subprocess.run([str(x) for x in command], cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=120)
    if log:
        log.write_text(result.stdout + result.stderr)
    if result.returncode:
        print(result.stdout + result.stderr)
        raise RuntimeError(f'Failed ({result.returncode}): {command[0]}')
    print(result.stdout.strip() if not log else f'{log.name}: passed')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gpu-build', type=Path)
    p.add_argument('--gpu-compile-only', action='store_true', help='With --gpu-build, prepare fixtures without starting GPU/SDL tests')
    p.add_argument('--cc', default='clang')
    p.add_argument('--out', type=Path, default=ROOT/'build/appendage-tests',
                   help='Separate fixture output directory; use a new path to preserve earlier evidence')
    args = p.parse_args()
    if sys.platform != 'linux':
        raise SystemExit('These POSIX fixtures require Linux; Windows runtime acceptance remains separate.')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('XCX_', 'LARECOMP_'))}
    env['XCX_APPENDAGE_GLYPH'] = '1'
    control = out / 'control'
    control.write_text('1')
    env['XCX_APPENDAGE_GLYPH_CONTROL'] = str(control)
    (out / 'layout.h').write_text('#define MOD_MAIN_BASE 0x500000000000ull\n')
    unit = out / 'unit'
    run([args.cc, '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
         '-I'+str(out), '-I'+str(ROOT/'runtime'), ROOT/'tests/appendage_glyph_test.c', ROOT/'runtime/input_glyphs.c', ROOT/'runtime/glyph_art.c', ROOT/'runtime/input_map.c', '-lzstd', '-lm', '-o', unit])
    policy = out/'policy'
    run([args.cc, '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-I'+str(ROOT/'runtime'), ROOT/'tests/input_glyphs_test.c', ROOT/'runtime/input_glyphs.c', ROOT/'runtime/glyph_art.c', ROOT/'runtime/input_map.c', '-lzstd', '-lm', '-o', policy])
    run([policy], env, out/'policy.log')
    run([sys.executable, ROOT/'tools/make_glyph_styles.py', '--out', out/'styles.inc'])
    assert (out/'styles.inc').read_bytes() == (ROOT/'runtime/glyph_art.inc').read_bytes()
    run([unit, 'enabled'], env, out/'unit-enabled.log')
    run([unit, 'guides'], env, out/'unit-guides.log')
    run([unit, 'resources'], env, out/'unit-resources.log')
    run([unit, 'icon'], env, out/'unit-icon.log')
    run([unit, 'inline'], env, out/'unit-inline.log')
    run([unit, 'disabled'], dict(env, XCX_APPENDAGE_GLYPH='0'), out/'unit-disabled.log')
    run([sys.executable, ROOT/'tools/make_appendage_glyph.py', '--out', out/'pixels.inc'])
    assert (out/'pixels.inc').read_bytes() == (ROOT/'runtime/appendage_r1.inc').read_bytes()
    sys.path.insert(0, str(ROOT/'tools'))
    from make_appendage_glyph import pixels
    from PIL import Image
    asset = ROOT/'assets/appendage-glyph/playstation_trigger_r1.png'
    surface = pixels(asset)
    assert surface.crop((384,64,448,128)).tobytes() == Image.open(asset).convert('RGBA').tobytes()
    assert all(not any(surface.getpixel((x,y))) for y in range(192) for x in range(512)
               if not (384 <= x < 448 and 64 <= y < 128))
    assert hashlib.sha256(surface.tobytes()).hexdigest() == '7b8c592420373543f21fdcf90aa1fc8e7c39712d445c235fb31fa1c01d994ab0'
    # The atlas record table is a closed form; its SHA-256 is the identity constant the runtime checks (inline_glyph.h).
    import re, struct
    def f32(x): return struct.unpack('<f', struct.pack('<f', x))[0]
    table = b''.join(struct.pack('<ffffff', 1.0, 1.0, f32(45/368), f32(149/900), f32((i % 8*46 + 0.5)/368), f32((i//8*50 + 0.5)/300)) + struct.pack('<HH', 46, 50) for i in range(48))
    header = (ROOT/'runtime/inline_glyph.h').read_text()
    constant = bytes(int(x, 16) for x in re.search(r'INLINE_RECORDS_SHA256 \{([^}]*)\}', header.replace('\\\n', ' ')).group(1).replace(' ', '').split(',') if x)
    assert len(table) == 1344 and hashlib.sha256(table).digest() == constant, 'closed-form atlas table does not match INLINE_RECORDS_SHA256'
    # The icon_button sheet's table is a closed form too: 8x3 cells of 64x64 in a 512x192 image. Its SHA-256 is the identity constant,
    # and records 14, 6, 7 / 5 / 15 of the closed form reproduce the three HUD hashes the accepted route verified on real runs
    # (appendage_glyph.c records_match), which ties this derivation to the live resource without any game data.
    sheet = b''.join(struct.pack('<ffffff', 1.0, 1.0, f32(63/512), f32(63/192), f32((i % 8*64 + 0.5)/512), f32((i//8*64 + 0.5)/192)) + struct.pack('<HH', 64, 64) for i in range(24))
    sheet_constant = bytes(int(x, 16) for x in re.search(r'SHEET_RECORDS_SHA256 \{([^}]*)\}', header).group(1).replace(' ', '').split(',') if x)
    assert len(sheet) == 672 and hashlib.sha256(sheet).digest() == sheet_constant, 'closed-form sheet table does not match SHEET_RECORDS_SHA256'
    glyph_c = (ROOT/'runtime/appendage_glyph.c').read_text()
    def runtime_hash(name):
        return bytes(int(x, 16) for x in re.search(name + r'\[32\] = \{([^}]*)\}', glyph_c, re.S).group(1).replace('\n', '').replace(' ', '').split(',') if x)
    selectors = [int(x) for x in re.search(r'selectors\[NODE_SLOTS\] = \{([^}]*)\}', glyph_c).group(1).split(',')]
    rec = lambda i: sheet[i*28:(i+1)*28]
    assert hashlib.sha256(b''.join(rec(i) for i in selectors[:3])).digest() == runtime_hash('expected'), 'sheet records 14, 6, 7 differ from the HUD runtime constant'
    assert hashlib.sha256(rec(5)).digest() == runtime_hash('x_record') and hashlib.sha256(rec(15)).digest() == runtime_hash('zl_record')
    # The stick-direction image (icon_button02): 4 cells of 88x88 in a 352x88 image. Closed form, SHA-256 constant, and its guard
    # constant is the one the opt-in resource probe already carried for icon_button02 (appendage_glyph.c probe_prefixes[2]).
    sticks = b''.join(struct.pack('<ffffff', 1.0, 1.0, f32(87.75/352), f32(87/88), f32((i*88 + 0.5)/352), f32(0.5/88)) + struct.pack('<HH', 88, 88) for i in range(4))
    def header_hash(name):
        return bytes(int(x, 16) for x in re.search(name + r' \{([^}]*)\}', header.replace('\\\n', ' ')).group(1).replace(' ', '').split(',') if x)
    assert len(sticks) == 112 and hashlib.sha256(sticks).digest() == header_hash('STICKS_RECORDS_SHA256'), 'closed-form stick table does not match STICKS_RECORDS_SHA256'
    probe_rows = re.search(r'probe_prefixes\[4\]\[32\] = \{(.*?)\n\};', glyph_c, re.S).group(1)
    probe2 = [bytes(int(x, 16) for x in m.replace('\n', '').replace(' ', '').split(',') if x) for m in re.findall(r'\{([^{}]*)\}', probe_rows)][2]
    assert header_hash('STICKS_GUARD_SHA256') == probe2, 'stick image guard differs from the probe constant for icon_button02'
    # Native glyph boxes: every box lies inside its cell; the replaceable symbols (not power 1, home 2, capture 23) have one.
    def boxes(name):
        body = re.search(name + r'[^=]*= \{(.*?)\n\};', header, re.S).group(1) if name != 'inline_stick_boxes' else re.search(name + r'[^=]*= (\{.*?\});', header, re.S).group(1)
        return [tuple(int(v) for v in m) for m in re.findall(r'\{(\d+), (\d+), (\d+), (\d+)\}', body)]
    font, sheet_boxes, stick_boxes = boxes('inline_font_boxes'), boxes('inline_sheet_boxes'), boxes('inline_stick_boxes')
    assert len(font) == 48 and len(sheet_boxes) == 24 and len(stick_boxes) == 4
    for k, (x, y, w, h) in enumerate(font):
        assert w > 0 and h > 0 and x + w <= 46 and y + h <= 50, ('font box', k)
    for k, (x, y, w, h) in enumerate(sheet_boxes):
        assert w > 0 and h > 0 and x + w <= 64 and y + h <= 64, ('sheet box', k)
    for k, (x, y, w, h) in enumerate(stick_boxes):
        assert w > 0 and h > 0 and x + w <= 88 and y + h <= 88, ('stick box', k)
    # The same 24 symbols in the same order in both layouts and in the policy enum; the four stick-image symbols follow them.
    policy = (ROOT/'runtime/input_glyphs.h').read_text()
    enum_text = re.search(r'enum \{ SYM_CRS,(.*?)GLYPH_SYMBOLS', policy, re.S).group(0)
    order = re.findall(r'SYM_([A-Z0-9]+)', enum_text.split('SYM_PER_SHEET')[0])
    assert order == 'CRS POW HOM MNS PLS X Y A B ANALP ANARP ANAL ANAR L R ZL ZR CRSU CRSD CRSL CRSR CRSUD CRSLR CAP'.split(), order
    assert 'SYM_SLV = SYM_PER_SHEET, SYM_SLH, SYM_SRV, SYM_SRH' in enum_text
    spec = importlib.util.spec_from_file_location('appendage', ROOT/'recomp/appendage.py')
    hooks = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(hooks)
    class Nso:
        build_id = hooks.BUILD
        source_sha256 = hooks.SHA256
    class Module:
        name = 'main'
        base = 0x7000000000
        nso = Nso()
        def word(self, address):
            return hooks.WORDS[address-self.base]
    module = Module()
    assert hooks.verified(module)
    module.nso.source_sha256 = 'wrong'
    try:
        hooks.verified(module)
        raise AssertionError('Hash mismatch accepted')
    except ValueError:
        pass
    module.nso.source_sha256 = hooks.SHA256
    module.word = lambda address: 0
    try:
        hooks.verified(module)
        raise AssertionError('Instruction mismatch accepted')
    except ValueError:
        pass
    module.nso.build_id = 'unsupported'
    assert not hooks.verified(module)
    assert hooks.call(module, module.base+0x60f68c, module.base+0x917fc0).startswith('appendage_glyph_attach')
    assert 'appendage_glyph_bind' in hooks.call(module, module.base+0xa27678, module.base+0xa47720)
    for site in (0x5a665c,0x5c6ed4):
        assert 'appendage_glyph_guide' not in hooks.call(module,module.base+site,module.base+0x5da974)
    assert 'appendage_glyph_lookup_attach' in hooks.call(module,module.base+0x55ff1c,module.base+0xa1cfc8)
    assert 'appendage_glyph_' not in hooks.call(module,module.base+0x55ff20,module.base+0xa1cfc8)
    assert hooks.entry(module, module.base+0xa2fac0) and hooks.entry(module, module.base+0xa2fcd0)
    # Icon probe sites: the ML:icon layout method entry and the two binds of the icon source. Everything else keeps its
    # original call, including the superseded 0xa21750 (vtable slot +0x78), the glyph-pass binds and the node+0x3c8 bind.
    assert 'appendage_glyph_icon_layout' in hooks.entry(module, module.base+0xa21afc)
    for other in (0xa21750, 0xa21afc+4, 0xa21e94):
        assert not hooks.entry(module, module.base+other)
    for site in (0xa325f8, 0xa32688):
        assert 'appendage_glyph_icon_bind(c, f_7000a47720)' in hooks.call(module, module.base+site, module.base+0xa47720)
    for site in (0xa32414, 0xa324c4, 0xa32540, 0xa32568, 0xa32770):
        assert 'appendage_glyph_' not in hooks.call(module, module.base+site, module.base+0xa47720)
    assert {0xa21afc, 0xa325f8, 0xa32688} <= set(hooks.WORDS)
    assert not ({0xa21750, 0xa32414, 0xa324c4, 0xa32540, 0xa32770} & set(hooks.WORDS))
    try:
        hooks.call(module, module.base+0xa325f8, module.base+0xa47724)
        raise AssertionError('Wrong bind target accepted')
    except AssertionError as error:
        assert str(error) != 'Wrong bind target accepted'
    assert not hooks.entry(module, module.base+0xa2fac4)
    print('Licensed pixels, exact hook identity/rejection and unrelated entry checks passed')
    if not args.gpu_build:
        return
    build = args.gpu_build.resolve()
    obj = out/'gpu.o'
    run([args.cc, '-std=gnu11', '-O1', '-g', '-I'+str(ROOT/'runtime'),
         '-I'+str(ROOT/'generated/upd'), '-I'+str(build), '-I'+str(ROOT/'toolchain/vma'),
         '-I'+str(ROOT/'runtime/third_party/imgui'), '-c', ROOT/'tests/appendage_glyph_gpu_test.c', '-o', obj])
    objects = sorted(x for x in (build/'obj').rglob('*.o') if x.name != 'vk_exec_draw.o')
    assert objects and any(x.name == 'appendage_glyph.o' for x in objects)
    rsp = out/'gpu.rsp'
    rsp.write_text('\n'.join('"'+str(x)+'"' for x in objects+[obj]))
    gpu = out/'gpu'
    run([args.cc, '-fuse-ld=lld', '-no-pie', '-Wl,-z,noexecstack', '-Wl,--wrap=main', '-Wl,--wrap=sha256', '-Wl,--wrap=input_glyph_snapshot',
         '@'+str(rsp), '-lvulkan', '-lSDL3', '-lopus', '-lvpx', '-lzstd', '-l:libstdc++.a', '-lm',
         '-lpthread', '-ldl', '-Wl,-rpath,'+str(build), '-o', gpu])
    sdl_obj = out/'sdl.o'
    run([args.cc, '-std=gnu11', '-O1', '-g', '-I'+str(ROOT/'runtime'), '-c', ROOT/'tests/input_glyphs_sdl_test.c', '-o', sdl_obj])
    sdl_objects = sorted(x for x in (build/'obj').rglob('*.o') if x.name != 'input_sdl.o')
    sdl_rsp = out/'sdl.rsp'
    sdl_rsp.write_text('\n'.join('"'+str(x)+'"' for x in sdl_objects+[sdl_obj]))
    sdl = out/'sdl'
    run([args.cc, '-fuse-ld=lld', '-no-pie', '-Wl,--wrap=main', '@'+str(sdl_rsp), '-lvulkan', '-lSDL3', '-lopus', '-lvpx', '-lzstd', '-l:libstdc++.a', '-lm', '-lpthread', '-ldl', '-Wl,-rpath,'+str(build), '-o', sdl])
    if args.gpu_compile_only:
        print('GPU and SDL fixtures compiled only; no GPU/SDL tests started.')
        return
    run([sdl],env,out/'sdl.log')
    for stage in ('normal', 'pixels', 'image', 'upload', 'blit', 'view', 'source'):
        stage_env = dict(env, XCX_VK_VALIDATION='1', GLYPH_TEST_CONFIG=str(out/f'settings-{stage}.ini'))
        if stage != 'normal':
            stage_env['XCX_APPENDAGE_GLYPH_FAIL'] = stage
        run([gpu, stage], stage_env, out/f'gpu-{stage}.log')
    print('Read each GPU log for validation availability; zero reported errors alone does not establish layer coverage.')


if __name__ == '__main__':
    main()
