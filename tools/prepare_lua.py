"""Verify pristine Lua/patch pins; prepare a reproducible patched build copy."""
import hashlib
import json
from pathlib import Path
import shutil
import re
ROOT=Path(__file__).resolve().parents[1]
DEP=ROOT/'deps/lua'

def prepare(out=None):
    out=Path(out or ROOT/'build/lua-src').resolve()
    pins=json.loads((DEP/'pins.json').read_text())
    for name,digest in pins['files'].items():
        if hashlib.sha256((DEP/name).read_bytes()).hexdigest()!=digest: raise ValueError('Lua pin mismatch: '+name)
    expected={n.removeprefix('pristine/') for n in pins['files'] if n.startswith('pristine/')}
    actual={p.relative_to(DEP/'pristine').as_posix() for p in (DEP/'pristine').rglob('*') if p.is_file()}
    if expected!=actual: raise ValueError('Lua pristine file inventory mismatch')
    # Stage beside destination; publish only after every recorded patch applies.
    stage=out.with_name(out.name+'.preparing')
    if stage.exists(): shutil.rmtree(stage)
    shutil.copytree(DEP/'pristine/src',stage)
    for name in pins['patches']:
        raw=(DEP/name).read_text()
        chunks=raw.split('diff --git ')[1:]
        for chunk in chunks:
            first=chunk.splitlines()[0].split()[0]
            name=first[2:]
            if not name.endswith('.c') or '/' in name:
                continue
            target=stage/name
            contents=target.read_text()
            hunks=re.split(r'^@@[^\n]*@@[^\n]*\n',chunk,flags=re.MULTILINE)[1:]
            for hunk in hunks:
                old=[]; new=[]
                for line in hunk.splitlines(keepends=True):
                    if line.startswith(' '): old.append(line[1:]); new.append(line[1:])
                    elif line.startswith('-') and not line.startswith('-- '): old.append(line[1:])
                    elif line.startswith('+'): new.append(line[1:])
                    else: break
                old=''.join(old); new=''.join(new)
                if not old or contents.count(old)!=1:
                    raise ValueError('Non-unique or mismatched upstream patch context: '+name)
                contents=contents.replace(old,new,1)
            target.write_text(contents)
    for p in stage.iterdir():
        if p.is_file() and out.exists() and (out/p.name).is_file() and p.read_bytes()==(out/p.name).read_bytes():
            shutil.copystat(out/p.name,p)
    if out.exists(): shutil.rmtree(out)
    stage.rename(out)
    return out

if __name__=='__main__': print(prepare())
