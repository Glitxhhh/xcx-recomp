"""Checks the icon probe's record lookup against the lifted guest function it describes.

Extracts f_7000a204cc and f_7000a21afc from the regenerated guest C (never stored in Git), compiles them with
tests/inline_icon_oracle.c and runs the perturbation oracle. Needs generated/<ver>/main from tools/gen; skips with a
non-zero status unless --allow-skip is given.

Usage: inline_icon_oracle.py --cc build/trial-clang --out build/glyph-inline-oracle [--gen generated/upd]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from check_content import GENERATED_MARKER  # noqa: E402

FUNCTIONS = ('f_7000a204cc', 'f_7000a21afc')


def extract(gen, name):
    for chunk in sorted((gen/'main').glob('main_0*.c')):
        out = None
        for line in chunk.read_text(errors='replace').splitlines(keepends=True):
            if line.startswith(f'void {name}(Cpu *c) {{'):
                out = [line]
            elif out is not None:
                if line.startswith('void f_'):
                    break
                out.append(line)
        if out:
            return ''.join(out)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--gen', type=Path, default=ROOT/'generated/upd')
    parser.add_argument('--allow-skip', action='store_true')
    args = parser.parse_args()
    gen = args.gen if args.gen.is_absolute() else ROOT/args.gen
    texts = [extract(gen, name) for name in FUNCTIONS] if (gen/'main').is_dir() else [None, None]
    if not all(texts):
        print('SKIPPED: regenerated guest C not present at', gen)
        return 0 if args.allow_skip else 2
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # The functions must be the ones the generator emits for the pinned game, with the entry hook only where intended.
    assert 'appendage_glyph_icon_layout(c);' in texts[1], 'layout hook missing from f_7000a21afc'
    assert 'appendage_glyph_icon_layout' not in texts[0], 'hook leaked into the layouter initialiser'
    assert not re.search(r'\bf_7[0-9a-f]+\(c\)', ''.join(texts).replace('void f_7000a204cc(Cpu *c)', '').replace('void f_7000a21afc(Cpu *c)', '')), \
        'extracted functions call other guest functions; the oracle would be incomplete'
    (out/'oracle_fn.inc').write_text(f'// {GENERATED_MARKER}: extracted at test time; do not commit.\n' + '\n'.join(texts))
    exe = out/'oracle'
    # UBSan only: the lifted code addresses the guest image at fixed absolute addresses (the GOT page at 0x7001d65000),
    # whose AddressSanitizer shadow does not exist in a host process.
    cmd = [str(args.cc), '-std=gnu11', '-O1', '-g', '-fno-strict-aliasing', '-fsanitize=undefined',
           '-I', str(ROOT/'runtime'), '-I', str(out), str(ROOT/'tests/inline_icon_oracle.c'), '-o', str(exe), '-lm']
    compiled = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    (out/'compile.log').write_text(compiled.stdout + compiled.stderr)
    if compiled.returncode:
        sys.stderr.write(compiled.stderr)
        raise SystemExit('oracle failed to compile')
    ran = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True, timeout=120)
    (out/'oracle.log').write_text(ran.stdout + ran.stderr)
    sys.stdout.write(ran.stdout)
    if ran.returncode:
        sys.stderr.write(ran.stderr)
        raise SystemExit(f'oracle failed ({ran.returncode})')
    print('oracle.log: passed')
    # Non-vacuity: each plausible misreading of the record layout must make the oracle fail.
    original = (ROOT/'runtime/inline_icon_probe.inc').read_text()
    mutations = {
        'stride 0x18': ('ICON_REC_BYTES = 0x1c', 'ICON_REC_BYTES = 0x18'),
        'count compare >=': ('out->count > key', 'out->count >= key'),
        'key masked to 8 bits': ('uint32_t key = code & 0xffff;', 'uint32_t key = code & 0xff;'),
        'record offset from +0x1c': ('rd32(out->table + 0x18)', 'rd32(out->table + 0x1c)'),
        'out of range does not fall back': ('out->count > key ? key : 0', 'key'),
    }
    for name, (find, replace) in mutations.items():
        assert original.count(find) == 1, name
        mutant = out/('mutant-' + re.sub(r'\W+', '-', name))
        mutant.mkdir(exist_ok=True)
        (mutant/'inline_icon_probe.inc').write_text(original.replace(find, replace))
        mexe = mutant/'oracle'
        built = subprocess.run(cmd[:cmd.index('-I')] + ['-I', str(mutant)] + cmd[cmd.index('-I'):-3] + ['-o', str(mexe), '-lm'],
                               cwd=ROOT, capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        mran = subprocess.run([str(mexe)], cwd=ROOT, capture_output=True, text=True, timeout=120)
        (mutant/'oracle.log').write_text(mran.stdout + mran.stderr)
        assert mran.returncode != 0, f'oracle accepted the mutated lookup: {name}'
        print(f'mutation rejected: {name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
