"""The eight setup steps of docs/installer.md: check, extract, dlc, lift, compile, prepare, dlss, cleanup.

Extraction runs in this process through tools/extract.py (the package's sibling tools/, which ships with it) and the
bundled hactool; lift and compile run the bundled Python on recomp/gen.py and tools/build_game.py, whose output goes to
<temp>/logs/<step>-<tool>.log and is read for progress. No Win32 call here: the host check is a small C program built
with the bundled clang (hostcheck.c).

Install folder: exefs/, romfs/ (romfs.bin, index.json), aoc/<index>/ (the add-on content's RomFS), profile/, xcx.exe
and its DLLs, nvngx_dlss.dll (with --dlss), romfs-index.bin, xcx_xlate.exe, xcx.ini, xcx-install.json.
Temp folder (<temp>/<dest name>-<hash>): check/ (ExeFS of both inputs, the host check), gen/ (C), build/ (objects),
logs/ (kept by the cleanup).
"""
import hashlib
import http.client
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

from . import extracted, ini, nsz
from .layout import APP_ID, BASE, EXE, GAME_EXE, SUPPORTED, version_tag
from .runner import InstallError, Step

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'tools'))
import extract  # noqa: E402
from mod_support import source_files  # noqa: E402
from extract_exefs import (common_tickets, container_entries, load_keys, nca_ctr_key, nca_inventory)  # noqa: E402
from package_select import program_nca  # noqa: E402
from extract_update_romfs import copy_range, hactool_prod_keys  # noqa: E402

GiB = 1 << 30
DEST_EXTRA = 4 * GiB      # beside the RomFS: shader and pipeline caches (about 2 GB), exefs, xcx.exe
TEMP_NEED = 3 * GiB       # generated C (about 0.9 GB) and objects
HOSTCHECK = Path(__file__).resolve().parent / 'hostcheck.c'
STAMP = '.install-key'
PATCHES = 'patches-upd.txt'   # recomp/: code patch sets of 2.0.0 (recomp/patches.py)
XLATE = 'xcx_xlate' + EXE   # the translator's name beside xcx.exe; runtime/gfx_shader.c starts it by this name


# ---- helpers

def file_sha256(path, progress=None):
    """SHA-256 hex of a file; progress(done, total) after each block."""
    h = hashlib.sha256()
    total = path.stat().st_size
    done = 0
    with open(path, 'rb', buffering=0) as f:
        buf = bytearray(8 << 20)
        view = memoryview(buf)
        while n := f.readinto(buf):
            h.update(view[:n])
            done += n
            if progress:
                progress(done, total)
    return h.hexdigest()


def fingerprint(*parts):
    """SHA-256 over strings and the contents of files and directory trees (a missing path counts as missing)."""
    h = hashlib.sha256()
    for p in parts:
        if isinstance(p, Path):
            if p.is_dir():
                for f in sorted(p.rglob('*')):
                    if f.is_file() and '__pycache__' not in f.parts:
                        h.update(f.relative_to(p).as_posix().encode() + b'\0' + bytes.fromhex(file_sha256(f)))
            elif p.is_file():
                h.update(p.name.encode() + b'\0' + bytes.fromhex(file_sha256(p)))
            else:
                h.update(b'missing\0' + p.name.encode())
        else:
            h.update(str(p).encode())
        h.update(b'\1')
    return h.hexdigest()


def remove(path):
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    elif path.exists() or path.is_symlink():
        path.unlink()


def replace_file(src, dst):
    """Copy src over dst through a temporary name, so a cancelled copy never leaves half a file under dst's name."""
    tmp = dst.with_name(dst.name + '.tmp')
    shutil.copy2(src, tmp)
    os.replace(tmp, dst)


def kill_tree(proc):
    if proc.poll() is not None:
        return
    if os.name == 'nt':
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(proc.pid)], capture_output=True)
        return
    try:   # run_tool starts each tool in a session of its own: the group holds ninja, clang and the game
        os.killpg(proc.pid, signal.SIGKILL)
    except OSError:
        proc.kill()


def linux_glibc():
    """(major, minor) of the C library this Linux process runs on, None elsewhere or when it is not glibc."""
    try:
        name = os.confstr('CS_GNU_LIBC_VERSION') if sys.platform.startswith('linux') else None
    except (ValueError, OSError):
        return None
    m = re.match(r'glibc (\d+)\.(\d+)', name or '')
    return (int(m.group(1)), int(m.group(2))) if m else None


# The bundled clang, lld and the cryptography wheel are built against glibc 2.34 (tools/make_release_linux.py
# GLIBC_FLOOR); on an older one they cannot start
GLIBC_NEEDED = (2, 34)


def linux_kernel():
    """(major, minor) of the running Linux kernel, None elsewhere."""
    if not sys.platform.startswith('linux'):
        return None
    m = re.match(r'(\d+)\.(\d+)', os.uname().release)
    return (int(m.group(1)), int(m.group(2))) if m else None


def run_tool(ctx, step, script, args, parse=None, cwd=None):
    """Run a tool script with the bundled Python. Its output goes to <temp>/logs/<step>-<script>.log and, line by line,
    to parse(line). Returns (exit code, log path)."""
    logs = ctx.temp / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / f'{step}-{Path(script).stem}.log'
    command = [str(ctx.layout.python), '-c', extract.RUN_SCRIPT, str(script), *map(str, args)]
    ctx.reporter.log('run ' + ' '.join(command[3:]))
    env = dict(os.environ, PYTHONUNBUFFERED='1', PYTHONIOENCODING='utf-8')
    with open(log, 'w', encoding='utf-8', newline='\n') as out:
        proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=cwd or ctx.layout.root,
                                env=env, text=True, encoding='utf-8', errors='replace', bufsize=1,
                                start_new_session=os.name != 'nt')
        try:
            for line in proc.stdout:
                out.write(line)
                if parse:
                    parse(line.rstrip())
            code = proc.wait()
        except BaseException:
            kill_tree(proc)
            raise
    ctx.reporter.log(f'{Path(script).name}: exit code {code}; output in {log}')
    return code, log


def free_bytes(path):
    """Free bytes on the disk of path (its nearest existing parent) and that parent's device."""
    p = Path(path)
    while not p.exists():
        p = p.parent
    return shutil.disk_usage(p).free, os.stat(p).st_dev


def raise_if_damaged(error, name, path):
    """InstallError check.input_damaged when a reader's ValueError says the file's data fails its hashes or ends early
    (extract.DAMAGE_TEXT)."""
    if extract.DAMAGE_TEXT.search(str(error)):
        raise InstallError('check.input_damaged', input=name, path=str(path), detail=str(error)) from None


def plain_input(ctx, path):
    """The game file at path as the extraction reads it: the file itself, or for an NSZ or XCZ the plain NSP rebuilt
    from it in the temp folder (installer/nsz.py), made once per run. The options keep the player's own file, which the
    state records and the step keys hash."""
    path = Path(path)
    cache = ctx.__dict__.setdefault('plain_inputs', {})
    if path not in cache:
        rep = ctx.reporter
        try:
            cache[path] = nsz.plain_path(path, ctx.temp, lambda d, t: rep.progress('check', 'decompress', d, t, 'bytes'))
        except nsz.NszError as e:
            raise InstallError('check.input_damaged', input=path.name, path=str(path), detail=str(e)) from None
        if cache[path] != path:
            rep.log(f'nsz: {path.name} -> {cache[path].name}')
    return cache[path]


def game_version(ctx):
    """The update this install is built from: what the check identified, else the recorded one."""
    return ctx.state.get('version', SUPPORTED[0])


def expected_of(ctx, version):
    """The expected hashes of the base game (BASE) or an update (tools/data/expected-xcx-<version>.json)."""
    return json.loads(ctx.layout.expected(version).read_text(encoding='utf-8'))


def expected_update(ctx):
    return expected_of(ctx, game_version(ctx))


class Watch(threading.Thread):
    """Polls a measure() of bytes written while a long call runs and reports it as progress."""

    def __init__(self, reporter, step):
        super().__init__(daemon=True)
        self.reporter, self.step = reporter, step
        self.phase, self.measure, self.total = None, None, None
        self.stopped = threading.Event()

    def set(self, phase, measure=None, total=None):
        self.phase, self.measure, self.total = phase, measure, total
        self.reporter.progress(self.step, phase, 0 if total else None, total, 'bytes' if total else None, force=True)

    def run(self):
        while not self.stopped.wait(1.0):
            phase, measure, total = self.phase, self.measure, self.total
            if phase and measure:
                try:
                    done = min(measure(), total)
                except OSError:
                    continue
                self.reporter.progress(self.step, phase, done, total, 'bytes')

    def stop(self):
        self.stopped.set()


def size_of(paths):
    return sum(p.stat().st_size for p in paths if p.is_file())


# ---- 1. check

class Check(Step):
    id = 'check'
    always = True

    def run(self, ctx):
        layout, rep, dest = ctx.layout, ctx.reporter, ctx.dest
        missing = layout.missing()
        if missing:
            raise InstallError('check.release_incomplete', paths=missing[:5])
        if dest.exists() and not dest.is_dir():
            raise InstallError('check.dest_not_folder', path=str(dest))
        own = (ctx.temp, *ctx.temp.parents)   # a temp folder inside the install folder (installer/cli.py) is ours
        if ctx.mode == 'install' and dest.is_dir() and any(p not in own for p in dest.iterdir()):
            raise InstallError('check.dest_not_empty', path=str(dest))
        lang = ctx.options.lang or ctx.state.get('lang')
        if not lang:
            raise InstallError('check.lang_missing')
        if lang not in ini.LANGUAGES:
            raise InstallError('check.lang_unknown', lang=lang, known=', '.join(ini.LANGUAGES))
        if lang in ini.NEED_SHARED_FONTS:
            rep.warning('warn.lang_needs_fonts', lang=lang)
        ctx.lang = lang
        ctx.temp.mkdir(parents=True, exist_ok=True)

        rep.progress('check', 'host', force=True)
        self.host(ctx)
        rep.progress('check', 'verify', force=True)
        ctx.verify_kept('check')
        ncas = 0
        if ctx.will_run('extract'):
            rep.progress('check', 'inputs', force=True)
            ncas = self.inputs(ctx)
        self.space(ctx, ncas)
        if ctx.will_run('extract'):
            rep.progress('check', 'versions', force=True)
            self.versions(ctx)
        return {}

    def host(self, ctx):
        layout, work = ctx.layout, ctx.temp / 'check'
        glibc = linux_glibc()
        if glibc and glibc < GLIBC_NEEDED and not layout.dev:
            raise InstallError('check.glibc_old', version='.'.join(map(str, glibc)),
                               need='.'.join(map(str, GLIBC_NEEDED)))
        work.mkdir(parents=True, exist_ok=True)
        exe = work / ('hostcheck' + EXE)
        command = [str(layout.clang), '-O1', str(HOSTCHECK), '-o', str(exe)]
        if layout.deps:
            command += ['-I', str(layout.deps / 'include')]
        if layout.sysroot:
            command += [f'--sysroot={layout.sysroot}', '-fuse-ld=lld']
        if os.name != 'nt':
            command.append('-ldl')
        r = subprocess.run(command, capture_output=True, text=True, errors='replace')
        if r.returncode:
            ctx.reporter.log(r.stdout + r.stderr)
            raise InstallError('check.toolchain_broken', detail=(r.stderr or r.stdout).strip()[-400:])
        r = subprocess.run([str(exe)], capture_output=True, text=True, errors='replace')
        try:
            host = json.loads(r.stdout)
        except ValueError:
            raise InstallError('check.host_probe_failed', code=r.returncode) from None
        ctx.reporter.log(f'host: {r.stdout.strip()}')
        if not host['x86_64_v3']:
            # Without AVX2 the game builds for x86-64-v2 (Compile, runtime/libxcxrt-v2.a), which runs slower
            if not host['x86_64_v2']:
                raise InstallError('check.cpu_too_old')
            if not layout.runtime_lib_v2.is_file():
                raise InstallError('check.cpu_no_avx2')
            ctx.reporter.warning('warn.cpu_no_avx2')
        if not host['loader']:
            raise InstallError('check.vulkan_missing')
        devices = host['devices']
        if not devices:
            raise InstallError('check.vulkan_no_device')
        if not any(tuple(d['api'][:2]) >= (1, 3) for d in devices):
            raise InstallError('check.vulkan_too_old',
                               devices=', '.join(f'{d["name"]} ({d["api"][0]}.{d["api"][1]})' for d in devices))
        kernel = linux_kernel()
        if kernel and kernel < (6, 7):
            # Without userfaultfd's asynchronous write protection nothing tracks the CPU's writes: the game copies whole
            # pools to the GPU at each submit and its texture checks see every page as changed (docs/building.md, Linux)
            ctx.reporter.warning('warn.kernel_old', version='.'.join(map(str, kernel)))
        ctx.host = host

    def inputs(self, ctx):
        """Open both files with the keys as the extraction will. Returns the bytes of both Program NCAs."""
        o = ctx.options
        if getattr(o, 'extracted', None):
            return self.extracted_inputs(ctx)
        for name in ('base', 'update', 'keys'):
            path = getattr(o, name)
            if not path:
                raise InstallError('check.inputs_needed')
            if not Path(path).is_file():
                raise InstallError('check.input_missing', input=name, path=str(path))
        try:
            keys = load_keys(Path(o.keys))
        except (ValueError, OSError, UnicodeDecodeError):
            raise InstallError('check.keys_unreadable') from None
        if 'header_key' not in keys:
            raise InstallError('check.keys_no_header_key')
        total = 0
        for name, is_update in (('base', False), ('update', True)):
            path = Path(getattr(o, name))
            with path.open('rb') as f:
                total += self.open_input(f, path, keys, name, is_update)
        for path in dlc_inputs(ctx):   # optional: each must hold this game's add-on content
            if not path.is_file():
                raise InstallError('check.input_missing', input='dlc', path=str(path))
            with path.open('rb') as f:
                dlc_record(f, path, keys)
        del keys
        return total

    @staticmethod
    def open_input(f, path, keys, name, is_update):
        try:
            kind, entries = container_entries(f, path.stat().st_size)
        except ValueError as e:
            raise_if_damaged(e, name, path)
            raise InstallError('check.not_game_file', input=name) from None
        entries = nsz.nca_entries(entries)   # an NSZ's headers read in place
        if is_update and kind != 'nsp':
            raise InstallError('check.update_not_nsp')
        try:
            tickets = common_tickets(f, entries)
        except ValueError as e:
            if 'Personalized' in str(e):
                raise InstallError('check.ticket_personalized', input=name) from None
            raise_if_damaged(e, name, path)
            raise InstallError('check.file_unreadable', input=name, detail=str(e)) from None
        try:
            records = nca_inventory(f, entries, keys)
        except ValueError as e:
            if 'NCA3' in str(e):
                raise InstallError('check.keys_wrong', input=name) from None
            raise_if_damaged(e, name, path)
            raise InstallError('check.file_unreadable', input=name, detail=str(e)) from None
        try:
            record, header = program_nca(records, update=is_update)
        except ValueError as e:
            if is_update and 'BKTR' in str(e):
                raise InstallError('check.update_is_base') from None
            raise InstallError('check.update_not_game' if is_update else 'check.base_not_game', detail=str(e)) from None
        try:
            nca_ctr_key(header, keys, tickets)
        except ValueError as e:
            if 'ticket' in str(e):
                raise InstallError('check.ticket_missing', input=name) from None
            raise InstallError('check.keys_too_old', input=name,
                               generation=max(header[0x206], header[0x220])) from None
        return record['size']

    def extracted_inputs(self, ctx):
        """An extracted game (installer/extracted.py): loose RomFS files need their table; add-on content still opens
        with the keys. Returns 0: no NCA is copied out."""
        o = ctx.options
        if 'loose' in o.extracted and not extracted.table_of(ctx.layout, SUPPORTED[0]):
            raise InstallError('check.romfs_no_table', version=SUPPORTED[0])
        files = dlc_inputs(ctx)
        if files:
            if not o.keys or not Path(o.keys).is_file():
                raise InstallError('check.inputs_needed')
            keys = load_keys(Path(o.keys))
            for path in files:
                if not path.is_file():
                    raise InstallError('check.input_missing', input='dlc', path=str(path))
                with path.open('rb') as f:
                    dlc_record(f, path, keys)
        return 0

    def space(self, ctx, ncas):
        dest_need = temp_need = 0
        if ctx.will_run('extract'):
            # the update is identified after this estimate: the larger RomFS of the supported ones
            dest_need += max(expected_of(ctx, v)['romfs']['size'] for v in SUPPORTED) + ncas
            romfs = ctx.dest / 'romfs' / 'romfs.bin'
            if romfs.is_file():   # a resumed extraction replaces it
                dest_need -= romfs.stat().st_size
        if ctx.will_run('prepare') or ctx.will_run('extract'):
            dest_need += DEST_EXTRA
        if ctx.will_run('lift') or ctx.will_run('compile'):
            temp_need += TEMP_NEED
        if ctx.will_run('extract'):   # an NSZ or XCZ is rebuilt as a plain NSP in the temp folder first
            o = ctx.options
            temp_need += sum(nsz.plain_size(p) for p in (o.base, o.update, *dlc_inputs(ctx)) if p)
        dest_free, dest_dev = free_bytes(ctx.dest)
        temp_free, temp_dev = free_bytes(ctx.temp)
        ctx.reporter.log(f'space: install folder needs {dest_need / GiB:.1f} GiB of {dest_free / GiB:.1f} free, '
                         f'temp folder {temp_need / GiB:.1f} GiB of {temp_free / GiB:.1f}')
        if dest_dev == temp_dev:
            dest_need += temp_need
            temp_need = 0
        ctx.reporter.emit('space', dest=str(ctx.dest), need=dest_need, free=dest_free, temp=str(ctx.temp),
                          temp_need=temp_need, temp_free=temp_free, same_disk=dest_dev == temp_dev)
        for path, need, free in ((ctx.dest, dest_need, dest_free), (ctx.temp, temp_need, temp_free)):
            if need > free:
                raise InstallError('check.disk_space', path=str(path), need_gb=round(need / 1e9, 1),
                                   free_gb=round(free / 1e9, 1))

    def versions(self, ctx):
        """Extract both ExeFS into the temp folder (verifying every block) and identify the versions by hash."""
        o, work = ctx.options, ctx.temp / 'check'
        logs = ctx.temp / 'logs'
        logs.mkdir(parents=True, exist_ok=True)
        versions = {v: expected_of(ctx, v) for v in SUPPORTED}
        if getattr(o, 'extracted', None):   # the update's ExeFS as it is, identified by the same hashes
            try:
                found = extract.identify_update(o.extracted['exefs'], versions)['version']
            except ValueError:
                found = 'unknown'
            if found not in SUPPORTED:
                raise InstallError('check.exefs_wrong_version', path=str(o.extracted['exefs']),
                                   wanted=' / '.join(SUPPORTED))
            self.set_version(ctx, found)
            return
        for name, is_update in (('base', False), ('update', True)):
            out = work / name
            remove(out)
            try:
                extract.extract_exefs(plain_input(ctx, getattr(o, name)), Path(o.keys), out, logs, update=is_update)
            except extract.DamagedFile as e:
                raise InstallError('check.input_damaged', input=name, path=str(e.path), detail=e.detail) from None
            except ValueError:
                raise InstallError('check.exefs_failed', input=name, log=str(logs / f'extract_exefs-{name}.log')) \
                    from None
        problems = extract.exefs_problems(work / 'base' / 'exefs', expected_of(ctx, BASE))
        if problems:
            raise InstallError('check.base_wrong_version', modules=', '.join(problems))
        try:
            found = extract.identify_update(work / 'update' / 'exefs', versions)['version']
        except ValueError:
            found = 'unknown'
        if found not in SUPPORTED:
            raise InstallError('check.update_wrong_version', found=found, wanted=' / '.join(SUPPORTED))
        self.set_version(ctx, found)

    @staticmethod
    def set_version(ctx, found):
        if found != game_version(ctx):   # another update than the recorded one: every step builds again from it
            ctx.reporter.log(f'check: update {found} (recorded {game_version(ctx)})')
            ctx.state['version'] = found
            ctx.keys = {}
            ctx.plan(ctx.forced | {'extract', 'dlc', 'lift', 'compile', 'prepare'})
        ctx.state['version'] = found


# ---- 2. extract

class Extract(Step):
    id = 'extract'

    def key(self, ctx):
        # The output is whatever matches the expected hashes, whichever files and tools produced it.
        return fingerprint('extract-1', ctx.layout.expected(game_version(ctx)))

    def current(self, ctx, record):
        # Another update file than the recorded one extracts again; the check then identifies it.
        update, known = getattr(ctx.options, 'update', None), ctx.state['inputs'].get('update')
        if update and known and Path(update).is_file():
            st = Path(update).stat()
            if (st.st_size, st.st_mtime_ns) != (known.get('size'), known.get('mtime_ns')) \
                    and file_sha256(Path(update)) != known.get('sha256'):
                return False
        expected = expected_update(ctx)
        romfs = ctx.dest / 'romfs'
        if extract.exefs_problems(ctx.dest / 'exefs', expected) or not extract.romfs_verified(romfs, expected):
            return False
        st = (romfs / 'romfs.bin').stat()
        return st.st_size == record.get('romfs_size') and st.st_mtime_ns == record.get('romfs_mtime_ns')

    def verify(self, ctx, record):
        image = ctx.dest / 'romfs' / 'romfs.bin'
        rep = ctx.reporter
        digest = file_sha256(image, lambda d, t: rep.progress('check', 'verify_romfs', d, t, 'bytes'))
        report = json.loads((ctx.dest / 'romfs' / 'index.json').read_text())
        if report.get('source_sha256'):   # packed from loose files (installer/extracted.py): its own hash
            return digest == report['romfs_sha256'] and extract.romfs_matches(report, expected_update(ctx))
        return digest == expected_update(ctx)['romfs']['sha256']

    def run(self, ctx):
        if getattr(ctx.options, 'extracted', None):
            return self.run_extracted(ctx)
        o, dest, rep = ctx.options, ctx.dest, ctx.reporter
        expected = expected_update(ctx)
        base, update, keys = Path(o.base), Path(o.update), Path(o.keys)   # the player's files: what the state records
        inputs = {}
        for name, path in (('base', base), ('update', update)):
            known = ctx.state['inputs'].get(name, {})
            st = path.stat()
            if known.get('size') == st.st_size and known.get('mtime_ns') == st.st_mtime_ns and known.get('sha256'):
                inputs[name] = known
                continue
            digest = file_sha256(path, lambda d, t, n=name: rep.progress('extract', 'hash_' + n, d, t, 'bytes'))
            inputs[name] = {'name': path.name, 'size': st.st_size, 'mtime_ns': st.st_mtime_ns, 'sha256': digest}
        ctx.state['inputs'] = inputs
        ctx.save()

        dest.mkdir(parents=True, exist_ok=True)
        for stale in dest.glob('update-merge-*'):   # a killed merge leaves its NCA copies (and key copies) behind
            remove(stale)
        rep.progress('extract', 'exefs', force=True)
        staged = ctx.temp / 'check' / 'update' / 'exefs'
        if not staged.is_dir():
            remove(ctx.temp / 'check' / 'update')
            try:
                extract.extract_exefs(plain_input(ctx, update), keys, ctx.temp / 'check' / 'update', ctx.temp / 'logs',
                                      update=True)
            except extract.DamagedFile as e:
                raise InstallError('check.input_damaged', input='update', path=str(e.path), detail=e.detail) from None
            except ValueError:
                raise InstallError('check.exefs_failed', input='update',
                                   log=str(ctx.temp / 'logs' / 'extract_exefs-update.log')) from None
        remove(dest / 'exefs')
        shutil.move(staged, dest / 'exefs')
        problems = extract.exefs_problems(dest / 'exefs', expected)
        if problems:
            raise InstallError('extract.exefs_mismatch', version=game_version(ctx), modules=', '.join(problems))
        if extract.sha256(dest / 'exefs' / 'main.npdm') != expected['exefs_sha256']['main.npdm']:
            rep.warning('warn.npdm_differs', version=game_version(ctx))

        romfs = dest / 'romfs'
        if self.id in ctx.forced:
            remove(romfs)   # the full hash failed: index.json still claims the extraction's hash
        if not extract.romfs_verified(romfs, expected):
            self.merge(ctx, plain_input(ctx, base), plain_input(ctx, update), keys, romfs, expected)
        report = json.loads((romfs / 'index.json').read_text())
        if report['romfs_size'] != expected['romfs']['size'] or report['romfs_sha256'] != expected['romfs']['sha256']:
            raise InstallError('extract.romfs_mismatch', version=game_version(ctx), size=report['romfs_size'],
                               sha256=report['romfs_sha256'][:16])
        (dest / 'profile').mkdir(exist_ok=True)
        st = (romfs / 'romfs.bin').stat()
        return {'romfs_size': st.st_size, 'romfs_mtime_ns': st.st_mtime_ns}

    def run_extracted(self, ctx):
        """An extracted game (installer/extracted.py): its ExeFS copied, its RomFS image taken or its loose files
        checked and packed."""
        found, dest, rep = ctx.options.extracted, ctx.dest, ctx.reporter
        expected = expected_update(ctx)
        dest.mkdir(parents=True, exist_ok=True)
        rep.progress('extract', 'exefs', force=True)
        remove(dest / 'exefs')
        shutil.copytree(found['exefs'], dest / 'exefs')
        problems = extract.exefs_problems(dest / 'exefs', expected)
        if problems:
            raise InstallError('extract.exefs_mismatch', version=game_version(ctx), modules=', '.join(problems))
        romfs = dest / 'romfs'
        if self.id in ctx.forced:
            remove(romfs)
        if not extract.romfs_verified(romfs, expected):
            remove(romfs)
            if 'image' in found:
                from extract_romfs import index_romfs
                report = extracted.take_image(found['image'], romfs, index_romfs,
                                              lambda d, t: rep.progress('extract', 'index', d, t, 'bytes'))
                if not extract.romfs_matches(report, expected):
                    remove(romfs)
                    raise InstallError('extract.romfs_mismatch', version=game_version(ctx), size=report['romfs_size'],
                                       sha256=report['romfs_sha256'][:16])
            else:
                table = extracted.table_of(ctx.layout, game_version(ctx))
                files = extracted.check_loose(found['loose'], table,
                                              lambda d, t: rep.progress('extract', 'check_files', d, t, 'bytes'))
                extracted.pack_loose(files, table, romfs, lambda d, t: rep.progress('extract', 'pack', d, t, 'bytes'))
        ctx.state['inputs'] = {'extracted': str(found['exefs'].parent)}
        (dest / 'profile').mkdir(exist_ok=True)
        st = (romfs / 'romfs.bin').stat()
        return {'romfs_size': st.st_size, 'romfs_mtime_ns': st.st_mtime_ns}

    def merge(self, ctx, base, update, keys, romfs, expected):
        dest, logs = ctx.dest, ctx.temp / 'logs'
        ncas = extract.merge_space(base, update, keys)
        watch = Watch(ctx.reporter, 'extract')

        def phase(name):
            if name == 'copy':
                watch.set('copy', lambda: size_of(dest.glob('update-merge-*/*.nca')), ncas)
            elif name == 'merge':
                watch.set('merge', lambda: size_of(dest.glob('update-merge-*/result/romfs.bin')),
                          expected['romfs']['size'])
            else:
                watch.set(name)

        watch.start()
        try:
            extract.merge_update_romfs(base, update, keys, ctx.layout.hactool, romfs, logs, phase=phase)
        except extract.DamagedFile as e:
            raise InstallError('check.input_damaged', input='base' if e.path == base else 'update', path=str(e.path),
                               detail=e.detail) from None
        except ValueError as e:
            raise InstallError('extract.romfs_merge_failed', detail=str(e), log=str(logs / 'merge_romfs-'
                                                                                     f'{dest.name}.log')) from None
        finally:
            watch.stop()


# ---- 2b. add-on content

AOC_BASE = APP_ID + 0x1000
AOC_DATA = 5   # NCA content type of add-on content (PublicData)


def aoc_index(title_id):
    """The AddOnContentIndex of an add-on title id, or None when it is not one of this game's."""
    index = int(title_id, 16) - AOC_BASE
    return index if 0 < index < 2000 else None


def dlc_inputs(ctx):
    """The add-on content files chosen (--dlc, any number), else none."""
    return [Path(p) for p in (getattr(ctx.options, 'dlc', None) or [])]


def dlc_record(f, path, keys):
    """(title id, NCA record) of the add-on content in an opened NSP. InstallError when it holds none of this game's."""
    try:
        kind, entries = container_entries(f, path.stat().st_size)
        records = nca_inventory(f, nsz.nca_entries(entries), keys)
    except ValueError as e:
        raise_if_damaged(e, 'dlc', path)
        raise InstallError('check.file_unreadable', input='dlc', detail=str(e)) from None
    for record, _ in records:
        if record['content_type'] == AOC_DATA and aoc_index(record['title_id']) is not None:
            return record['title_id'], record
    raise InstallError('check.dlc_not_game', path=str(path))


class Dlc(Step):
    """Extracts the RomFS of each add-on content file into aoc/<index>/, where xcx.exe mounts it (runtime/fs_host.c
    mount_aoc). Without --dlc it keeps what an earlier run extracted."""
    id = 'dlc'

    def key(self, ctx):
        files = dlc_inputs(ctx)
        if not files:   # nothing chosen: whatever is recorded stays current
            return ctx.state.get('steps', {}).get('dlc', {}).get('key') or fingerprint('dlc-1')
        return fingerprint('dlc-1', *(f'{p.name}:{p.stat().st_size}:{p.stat().st_mtime_ns}' for p in files))

    def current(self, ctx, record):
        return all((ctx.dest / 'aoc' / str(i)).is_dir() for i in record.get('indices', []))

    def run(self, ctx):
        files, dest, rep = dlc_inputs(ctx), ctx.dest, ctx.reporter
        if not files:
            return {'indices': []}
        keys_path = Path(ctx.options.keys)
        keys = load_keys(keys_path)
        work = ctx.temp / 'dlc'
        remove(work)
        work.mkdir(parents=True)
        hactool_keys = work / 'hactool.keys'
        hactool_prod_keys(keys_path, hactool_keys)
        indices = []
        try:
            for n, path in enumerate(files):
                rep.progress('dlc', 'extract', n, len(files), 'files')
                plain = plain_input(ctx, path)
                with plain.open('rb') as f:
                    title_id, record = dlc_record(f, plain, keys)
                index = aoc_index(title_id)
                nca, out = work / f'{index}.nca', dest / 'aoc' / str(index)
                copy_range(plain, record['nca_offset'], record['size'], nca)
                remove(out)
                out.parent.mkdir(parents=True, exist_ok=True)
                code, log = extract.run_hactool([str(ctx.layout.hactool), '-y', '--disablekeywarns', '--suppresskeys',
                                                 '-k', str(hactool_keys), f'--romfsdir={out}', str(nca)], dict(os.environ))
                (ctx.temp / 'logs').mkdir(parents=True, exist_ok=True)
                (ctx.temp / 'logs' / f'dlc-{index}.log').write_text(log, encoding='utf-8')
                if code or not out.is_dir():
                    raise InstallError('dlc.extract_failed', path=str(path), log=str(ctx.temp / 'logs' / f'dlc-{index}.log'))
                rep.log(f'dlc: {path.name} is add-on content {index} ({title_id})')
                indices.append(index)
        finally:
            remove(hactool_keys)
        rep.progress('dlc', 'extract', len(files), len(files), 'files')
        return {'indices': sorted(set(indices))}


# ---- 3. lift

EMITTED = re.compile(r'^emitted (\d+)/(\d+) chunks')
DISCOVERED = re.compile(r'^(\w+): \d+ functions \(.*discovery')


CHEAT_PLACES = ('cheats', 'atmosphere/contents/0100453019AA8000/cheats', 'contents/0100453019AA8000/cheats',
                '0100453019AA8000/cheats')


def cheat_files(ctx):
    """The Atmosphere cheat files of the mods folder (<dest>/mods/<mod>/cheats/*.txt and the Atmosphere places, as
    tools/cheats.py finds them), for the lift key. All of them: the build reads the ones named after the installed
    main's build id, and the keys are computed before Extract runs, when a fresh install has no main yet."""
    mods = ctx.dest / 'mods'
    if not mods.is_dir():
        return []
    return sorted(f for mod in mods.iterdir() if mod.is_dir() for where in CHEAT_PLACES
                  for f in ((mod / where).glob('*.txt') if (mod / where).is_dir() else ()))


def builtin_cheats(ctx):
    """The port's own cheat lists (tools/data/cheats, one per build of main), which every install builds in."""
    folder = ctx.layout.tools / 'data' / 'cheats'
    return sorted(folder.glob('*.txt')) if folder.is_dir() else []


def cheats_status(ctx):
    """The build's cheat status file (tools/cheats.py --status), which runtime/cheats.c reads: which cheats became
    patch sets, which the cheat interpreter runs and which cannot work."""
    return ctx.dest / 'build-cache' / 'cheats.status'


class Lift(Step):
    id = 'lift'
    transient = True

    def key(self, ctx):
        # fingerprint takes a file's name and content only; the paths under mods/ go in too, because tools/cheats.py
        # names each patch set after its mod folder, so a renamed folder needs a lift
        files = cheat_files(ctx)
        return fingerprint('lift-5', ctx.keys['extract'], ctx.layout.recomp, ctx.layout.tools / 'cheats.py', *files,
                           *builtin_cheats(ctx), *(f.relative_to(ctx.dest / 'mods').as_posix() for f in files))

    def current(self, ctx, record):
        gen = ctx.temp / 'gen'
        stamp = gen / STAMP
        return (gen / 'layout.json').is_file() and stamp.is_file() and stamp.read_text().strip() == ctx.keys['lift']

    def run(self, ctx):
        layout, rep, gen = ctx.layout, ctx.reporter, ctx.temp / 'gen'
        remove(gen)
        rep.progress('lift', 'discover', force=True)

        def parse(line):
            m = EMITTED.match(line)
            if m:
                rep.progress('lift', 'emit', int(m.group(1)), int(m.group(2)), 'chunks')

        args = ['--exefs', ctx.dest / 'exefs', '--out', gen]
        if game_version(ctx) == '2.0.0':
            args += ['--mod-menu-hook']
        plain = layout.recomp / PATCHES   # the 60 fps sites and the other sets of 2.0.0
        # The code cheats of the built-in cheat list and of the mods' cheat files become patch sets, off until switched
        # on in the Cheats tab (tools/cheats.py); its status file tells the runtime what the build has. A failure costs
        # the cheats, not the install: the game is built without them and the setup warns.
        status = cheats_status(ctx)
        status.unlink(missing_ok=True)   # no status: runtime/cheats.c treats every code cheat as not built
        patches, skipped = plain, None
        if plain.is_file() and (builtin_cheats(ctx) or cheat_files(ctx)):
            code, log = run_tool(ctx, 'lift', layout.tools / 'cheats.py',
                                 ['--exefs', ctx.dest / 'exefs', '--mods', ctx.dest / 'mods',
                                  '--builtin', layout.tools / 'data' / 'cheats', '--patches', plain,
                                  '--out', ctx.temp / 'patches-cheats.txt', '--status', status])
            if code:
                status.unlink(missing_ok=True)
                skipped = log
            else:
                patches = ctx.temp / 'patches-cheats.txt'
        tables = ['--patches', patches] if patches.is_file() else []
        code, log = run_tool(ctx, 'lift', layout.recomp / 'gen.py', args + tables, parse)
        if code and patches != plain:
            status.unlink(missing_ok=True)
            skipped = log
            code, log = run_tool(ctx, 'lift', layout.recomp / 'gen.py',
                                 args + (['--patches', plain] if plain.is_file() else []), parse)
        if code:
            raise InstallError('lift.failed', code=code, log=str(log))
        if skipped:
            rep.warning('warn.cheats_skipped', log=str(skipped))
        (gen / STAMP).write_text(ctx.keys['lift'] + '\n')
        return {}


# ---- 4. compile

NINJA_LINE = re.compile(r'^\[(\d+)/(\d+)\] ')


class Compile(Step):
    id = 'compile'

    def key(self, ctx):
        layout = ctx.layout
        clang = subprocess.run([str(layout.clang), '--version'], capture_output=True, text=True).stdout
        tools = [layout.tools / n for n in ('build_game.py', 'build_shaders.py', 'romfs_index.py', 'check_content.py')]
        pgo = layout.pgo(game_version(ctx))
        return fingerprint('compile-2', ctx.keys['lift'], layout.runtime, self.runtime_lib(ctx), layout.deps or 'system',
                           layout.sysroot or 'host', *tools, clang, *[d.name for d in layout.dlls], pgo or 'no profile')

    @staticmethod
    def v3(ctx):
        """Build for x86-64-v3: the check found AVX2, or has not run (a check-only pass computes the keys first)."""
        host = getattr(ctx, 'host', None)
        return host is None or host['x86_64_v3']

    def runtime_lib(self, ctx):
        """The runtime library for this CPU: the x86-64-v2 one when the check found no AVX2."""
        return ctx.layout.runtime_lib if self.v3(ctx) else ctx.layout.runtime_lib_v2

    def current(self, ctx, record):
        exe = ctx.dest / GAME_EXE
        if not exe.is_file() or not (ctx.dest / 'romfs-index.bin').is_file():
            return False
        if not all((ctx.dest / d.name).is_file() for d in ctx.layout.dlls):
            return False
        return file_sha256(exe) == record.get('exe_sha256')

    def run(self, ctx):
        layout, rep, dest = ctx.layout, ctx.reporter, ctx.dest
        build = ctx.temp / 'build'
        phase = ['compile']

        def parse(line):
            m = NINJA_LINE.match(line)
            if m:
                rep.progress('compile', phase[0], int(m.group(1)), int(m.group(2)), 'objects')
            elif line.startswith('compile: '):
                phase[0] = 'link'
                rep.progress('compile', 'link', force=True)

        rep.progress('compile', 'compile', force=True)
        code, log = run_tool(ctx, 'compile', layout.tools / 'build_game.py', [
            '--gen', ctx.temp / 'gen', '--out', build, '--toolchain', layout.llvm,
            *(['--deps', layout.deps] if layout.deps else []), *(['--sysroot', layout.sysroot] if layout.sysroot else []),
            '--runtime', layout.runtime, '--runtime-lib', self.runtime_lib(ctx), '--ninja', layout.ninja,
            '--cpu', 'x86-64-v3' if self.v3(ctx) else 'x86-64-v2',
            '--version', version_tag(game_version(ctx)), '--expected', layout.expected(game_version(ctx)),
            '--exefs', dest / 'exefs', '--romfs', dest / 'romfs',
            '--profile', dest / 'profile', *(['--pgo', pgo] if (pgo := layout.pgo(game_version(ctx))) else [])], parse)
        exe = build / ('xcx' + EXE)
        if code or not exe.is_file():
            raise InstallError('compile.failed', code=code, log=str(log))
        try:
            replace_file(exe, dest / GAME_EXE)
        except PermissionError:
            raise InstallError('compile.exe_in_use', path=str(dest / GAME_EXE)) from None
        replace_file(build / 'romfs-index.bin', dest / 'romfs-index.bin')
        for dll in layout.dlls:
            replace_file(dll, dest / dll.name)
        return {'exe_sha256': file_sha256(dest / GAME_EXE)}


# ---- 5. prepare

class Prepare(Step):
    """Puts the shader translator beside xcx.exe. XCX has no shader list to translate ahead of play yet: shaders are
    translated on first use and cached in shader-cache/."""
    id = 'prepare'

    def key(self, ctx):
        layout = ctx.layout
        return fingerprint('prepare-3', ctx.keys['compile'], layout.xlate, layout.xlate.with_suffix('.id'),
                           *[src for src, _ in source_files(layout.root, layout.dev)])

    def current(self, ctx, record):
        xlate = ctx.dest / XLATE
        return (xlate.is_file() and file_sha256(xlate) == record.get('xlate_sha256')
                and all((ctx.dest / relative).is_file() and file_sha256(ctx.dest / relative) == file_sha256(src)
                        for src, relative in source_files(ctx.layout.root, ctx.layout.dev)))

    def run(self, ctx):
        layout, dest = ctx.layout, ctx.dest
        ctx.reporter.progress('prepare', 'translator', force=True)
        xlate = dest / XLATE
        if xlate.is_file() and file_sha256(xlate) != file_sha256(layout.xlate):
            remove(dest / 'shader-cache')   # another translator: its cache keys differ, the old ones only take space
        replace_file(layout.xlate, xlate)
        xlate_id = layout.xlate.with_suffix('.id')
        if xlate_id.is_file():
            replace_file(xlate_id, xlate.with_suffix('.id'))
        else:
            remove(xlate.with_suffix('.id'))
        for src, relative in source_files(layout.root, layout.dev):
            destination = dest / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            replace_file(src, destination)
        return {'xlate_sha256': file_sha256(xlate)}



# ---- 6. NVIDIA DLSS

# NVIDIA's DLSS library, which NGX loads from the folder of xcx.exe (runtime/vk_dlss.c). The setup does not ship it:
# with --dlss it downloads NVIDIA's own copy from the DLSS SDK repository (NVIDIA RTX SDK license) at a pinned tag and
# checks its hash. The game runs without it; upscaler = dlss then falls back. Ported from TotK Recomp installer/steps.py
# (Dlss, nvidia_rtx; MIT, Copyright (c) 2026 Valiantsin Dzerakh).
DLSS_DLL = 'nvngx_dlss.dll'
DLSS_VERSION = '310.9.1'
DLSS_URL = f'https://raw.githubusercontent.com/NVIDIA/DLSS/v{DLSS_VERSION}/lib/Windows_x86_64/rel/{DLSS_DLL}'
DLSS_SIZE = 58956912
DLSS_SHA256 = '3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983'
DLSS_LICENSE = 'https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt'


def nvidia_rtx(host):
    """An NVIDIA RTX GPU among the host check's Vulkan devices: the GPUs DLSS runs on."""
    return any('NVIDIA' in d['name'].upper() and 'RTX' in d['name'].upper() for d in (host or {}).get('devices', []))


class Dlss(Step):
    """With --dlss on Windows and an NVIDIA RTX GPU: nvngx_dlss.dll next to xcx.exe. A DLSS library the player put
    there (another version) is kept. A failed download warns and is tried again on the next run; the game runs
    without it."""

    id = 'dlss'

    def key(self, ctx):
        given = getattr(ctx.options, 'dlss_file', None)
        if given:
            return fingerprint('dlss-1', 'file', file_sha256(given) if given.is_file() else 'missing')
        if not getattr(ctx.options, 'dlss', False):
            return fingerprint('dlss-1', 'off')
        return fingerprint('dlss-1', DLSS_VERSION, DLSS_SHA256)

    def current(self, ctx, record):
        if record.get('failed'):
            return False
        return not record.get('installed') or (ctx.dest / DLSS_DLL).is_file()

    def run(self, ctx):
        given = getattr(ctx.options, 'dlss_file', None)
        if given:
            # A launcher fetched the file (--dlss-file): copy it over any library an older run left, but only when it
            # is the same NVIDIA release the download takes, so the setup never installs an unchecked DLL
            sha = file_sha256(given) if given.is_file() else 'missing'
            if sha != DLSS_SHA256:
                ctx.reporter.warning('warn.dlss_file_wrong', path=str(given), sha=sha[:16], version=DLSS_VERSION)
                return {'failed': True}
            replace_file(given, ctx.dest / DLSS_DLL)
            return {'installed': DLSS_VERSION}
        if not getattr(ctx.options, 'dlss', False):
            return {}
        if os.name != 'nt':
            return {'skipped': 'not_windows'}
        if not nvidia_rtx(getattr(ctx, 'host', None)):
            # The box is ticked by default, so most players without an RTX GPU never chose it: a log line, no warning
            ctx.reporter.log('dlss: no NVIDIA RTX GPU among the Vulkan devices, nothing downloaded')
            return {'skipped': 'no_rtx'}
        rep, target = ctx.reporter, ctx.dest / DLSS_DLL
        if target.is_file():
            if file_sha256(target) == DLSS_SHA256:
                return {'installed': DLSS_VERSION}
            rep.warning('warn.dlss_kept', path=str(target))
            return {'kept': True}
        part = ctx.temp / (DLSS_DLL + '.part')
        rep.progress('dlss', 'download', 0, DLSS_SIZE, 'bytes', force=True)
        rep.log(f'dlss: {DLSS_URL} (license {DLSS_LICENSE})')
        try:
            part.parent.mkdir(parents=True, exist_ok=True)
            h, done = hashlib.sha256(), 0
            with urllib.request.urlopen(DLSS_URL, timeout=60) as r, open(part, 'wb') as f:
                while block := r.read(1 << 20):
                    f.write(block)
                    h.update(block)
                    done += len(block)
                    rep.progress('dlss', 'download', min(done, DLSS_SIZE), DLSS_SIZE, 'bytes')
            if done != DLSS_SIZE or h.hexdigest() != DLSS_SHA256:
                raise OSError(f'{done} bytes, SHA-256 {h.hexdigest()[:16]}, not the expected file')
            replace_file(part, target)
            remove(part)
        # urllib's errors, timeouts and a bad file; a connection cut mid-file raises http.client.IncompleteRead,
        # which is not an OSError
        except (OSError, http.client.HTTPException) as e:
            remove(part)
            rep.warning('warn.dlss_download_failed', url=DLSS_URL, detail=str(e)[:300])
            return {'failed': True}
        return {'installed': DLSS_VERSION}


# ---- 7. clean up

class Cleanup(Step):
    id = 'cleanup'
    always = True

    def run(self, ctx):
        rep, dest = ctx.reporter, ctx.dest
        rep.progress('cleanup', 'ini', force=True)
        try:
            ini.write(dest, ctx.lang, ctx.layout.ini_example)
        except OSError as e:
            raise InstallError('cleanup.ini_failed', detail=str(e)) from None
        ctx.state['lang'] = ctx.lang
        (dest / 'profile').mkdir(exist_ok=True)
        rep.progress('cleanup', 'temp', force=True)
        for stale in dest.glob('update-merge-*'):
            remove(stale)
        for name in ('gen', 'build', 'check', 'dlc'):
            remove(ctx.temp / name)
        return {}


def all_steps():
    return [Check(), Extract(), Dlc(), Lift(), Compile(), Prepare(), Dlss(), Cleanup()]
