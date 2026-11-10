"""Link a private headless entry to a matching native build, then test a disposable installation.

Never starts the normal game entry (except --selftest). Requires a finished native Ninja build.
Use --exe to test an already linked private entry, not a normal game executable.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import time
ROOT = Path(__file__).resolve().parents[2]
WIN = os.name == 'nt'
EXT = '.exe' if WIN else ''


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--build', type=Path, help='Matching native build directory')
    mode.add_argument('--exe', type=Path, help='Existing private installed_driver entry only')
    parser.add_argument('--out', type=Path, required=True, help='New disposable destination; must not exist')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--build-root', type=Path, default=ROOT, help='Working directory used to create the native build')
    parser.add_argument('--provider', type=Path, help='Optional inert tests/actions c0 platform library')
    args = parser.parse_args()
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=False)
    exe = out / ('mod-install-check' + EXT)
    if args.build:
        work = args.build_root.resolve()
        build = args.build.resolve(); ninja = build / 'build.ninja'; lines = ninja.read_text().splitlines()
        edge = next(line for line in lines if ': cc_rt ' in line and line.endswith('mod_menu_adapter.c'))
        target = edge[6:].split(': cc_rt ', 1)[0]
        def command(target):
            return shlex.split(subprocess.check_output([args.ninja, '-f', str(ninja), '-t', 'commands', str(target)], cwd=work, text=True).splitlines()[-1])
        obj = out / 'driver.o'; cmd = command(target)
        for flag, value in [('-c', ROOT / 'tests/release/installed_driver.c'), ('-o', obj), ('-MF', out / 'driver.o.d')]:
            if flag in cmd: cmd[cmd.index(flag) + 1] = str(value)
        subprocess.run(cmd, cwd=work, check=True)
        edge = next(line for line in lines if ': link_lib ' in line or ': link ' in line)
        original, inputs = edge[6:].split(': ', 1); inputs = inputs.split(' ', 1)[1].split(' | ', 1)[0]
        rsp = out / 'check.rsp'; rsp.write_text(inputs.replace(' ', '\n'))
        cmd = command(original)
        cmd[cmd.index('-o') + 1] = str(exe)
        cmd = ['@' + str(rsp) if arg.startswith('@') else arg for arg in cmd]
        subprocess.run([*cmd, str(obj), '-Wl,--wrap=main'], cwd=work, check=True)
        for dll in build.glob('*.dll'): shutil.copy2(dll, out / dll.name)
        for lib in build.glob('libSDL3.so*'): shutil.copy2(lib, out / lib.name)
    else:
        shutil.copy2(args.exe.resolve(), exe)
    before = digest(exe)
    env = os.environ.copy(); env.pop('XCX_MOD_DIR', None)
    def run(*steps):
        p = subprocess.run([str(exe), *steps], env=env, cwd=out, capture_output=True, text=True, timeout=15)
        assert p.returncode == 0, p.stdout + p.stderr
        assert 'SHUTDOWN clean' in p.stdout, p.stdout + p.stderr
        return p.stdout + p.stderr
    # Keep this startup alive while files are installed: it must not discover them until restart.
    log = out / 'before-install.log'
    with log.open('w') as stream:
        live = subprocess.Popen([str(exe), 'hold'], cwd=out, env=env, stdin=subprocess.PIPE, stdout=stream, stderr=stream, text=True)
        try:
            deadline = time.monotonic() + 10
            while 'READY' not in log.read_text():
                assert live.poll() is None and time.monotonic() < deadline, log.read_text()
                time.sleep(.02)
            mods = out / 'mods'; shutil.copytree(ROOT / 'examples/mods/hello', mods / 'hello')
            (mods / 'catalog.txt').write_text('hello\n'); (mods / 'enabled.txt').write_text('hello\n')
            live.communicate('snapshot\nquit\n', timeout=10)
            assert live.returncode == 0, log.read_text()
            assert log.read_text().count('SNAPSHOT 0 capability=1') == 2, log.read_text()
        finally:
            if live.poll() is None: live.kill(); live.wait()
    assert 'PACKAGE hello status=1' in run('status')
    assert 'Hello! Clicks this run: 1' in run('action', 'hello', 'greet')
    assert 'save=Saved' in run('set', 'hello', '0')
    settings = out / 'mod-settings/hello.ini'; stored = settings.read_bytes()
    assert 'value=0 dirty=0' in run('status')
    (mods / 'enabled.txt').write_text('')
    disabled = run('status'); assert 'PACKAGE hello status=2' in disabled and 'Hello workshop loaded' not in disabled
    assert settings.read_bytes() == stored
    (mods / 'enabled.txt').write_text('hello\n')
    assert 'value=0 dirty=0' in run('status')
    script = mods / 'hello/main.lua'; script.write_text(script.read_text().replace('Choose Say hello.', 'Updated source loaded.'))
    assert 'Updated source loaded.' in run('status') and settings.read_bytes() == stored
    # Unsafe stored settings stay untouched at startup, and use defaults.
    settings.write_bytes(b'x' * 4097)
    rejected = run('status')
    assert 'value=1 dirty=0' in rejected and 'Stored settings unreadable or unsafe' in rejected and settings.stat().st_size == 4097
    settings.unlink(); settings.mkdir()
    rejected = run('status')
    assert 'value=1 dirty=0' in rejected and 'Stored settings unreadable or unsafe' in rejected and settings.is_dir()
    settings.rmdir(); settings.write_bytes(stored)
    provider_checked = False
    if args.provider:
        shutil.copytree(ROOT / 'tests/actions/packages/alpha', mods / 'alpha')
        target = 'windows-x86_64' if WIN else 'linux-x86_64'; name = 'provider.dll' if WIN else 'provider.so'
        library = mods / 'alpha/native' / target / name; library.parent.mkdir(parents=True); shutil.copy2(args.provider, library)
        (mods / 'alpha/provider.ini').write_text('schema=2\nid=alpha.native\nowner=alpha\nabi=2.0\nrequired=1\nhost=xcx-mods-service-2.0\ngame=2.0.0\n' + ('windows' if WIN else 'linux') + '-sha256=' + digest(library) + '\n')
        (mods / 'enabled.txt').write_text('hello\nalpha\n'); (mods / 'providers-enabled.txt').write_text('alpha:alpha.native\n')
        result = run('action', 'alpha', 'success')
        assert 'ok: Action completed' in result, result
        assert result.index('provider destroyed before Lua teardown') < result.index('alpha closed bytes=0 callbacks=0'), result
        provider_checked = True
    assert digest(exe) == before
    result = {'host_sha256_before': before, 'host_sha256_after': digest(exe), 'discovery_requires_restart': True,
              'enabled_disabled_reenabled': True, 'real_settings_persisted': True, 'unsafe_startup_settings_preserved': True,
              'script_update_without_rebuild': True, 'native_provider_checked': provider_checked, 'clean_shutdown': True,
              'native_platform': os.name, 'headless_private_entry': True}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
