"""Build and run native package/core tests, Linux coordinator tests and Lua regressions."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from prepare_lua import prepare

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default='clang')
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    lua=prepare(); build=ROOT/'build'; build.mkdir(exist_ok=True)
    flags=['-O1','-g','-std=gnu11','-Wall','-Wextra']
    if args.sanitize: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    core=[p for p in sorted(lua.glob('*.c')) if p.name not in ('lua.c','luac.c','linit.c') and (not p.name.endswith('lib.c') or p.name in ('lauxlib.c','lbaselib.c','lstrlib.c','ltablib.c','lmathlib.c'))]
    extension='.exe' if sys.platform=='win32' else ''
    env=os.environ.copy(); env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1'; env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    def run(cmd): subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,check=True)
    common=[args.cc,*flags,'-I'+str(lua),'-I'+str(ROOT/'runtime')]
    provider=[ROOT/'runtime/mod_menu.c',ROOT/'runtime/mod_provider.c',ROOT/'runtime/mod_provider_platform.c',ROOT/'runtime/sha256.c']
    run([*common,'-DXCX_MOD_TESTING',ROOT/'tests/mods/driver.c',ROOT/'runtime/mod_package.c',ROOT/'tests/mods/runtime_test.c',ROOT/'tests/mods/settings_test.c',ROOT/'tests/mods/settings_io.c',*provider,*core,'-lm','-o',build/('mod-test'+extension)])
    env['MOD_TEST_EXE']=str(build/('mod-test'+extension))
    run([*common,'-DXCX_MOD_TESTING',ROOT/'tests/mods/settings_driver.c',ROOT/'runtime/mod_package.c',ROOT/'tests/mods/runtime_test.c',ROOT/'tests/mods/settings_test.c',ROOT/'tests/mods/settings_io.c',*provider,*core,'-lm','-o',build/('mod-settings-test'+extension)])
    env['MOD_SETTINGS_EXE']=str(build/('mod-settings-test'+extension))
    if sys.platform=='linux':
        run([*common,'-DXCX_MOD_TESTING',ROOT/'tests/mods/lifecycle_driver.c',ROOT/'tests/mods/platform_stub.c',ROOT/'runtime/mods.c',ROOT/'runtime/mod_package.c',ROOT/'tests/mods/runtime_test.c',ROOT/'tests/mods/settings_test.c',ROOT/'tests/mods/settings_io.c',*provider,*core,'-lm','-pthread','-o',build/'mod-lifecycle-test'])
        env['MOD_LIFECYCLE_EXE']=str(build/'mod-lifecycle-test')
        run([*common,'-DXCX_MOD_TESTING',ROOT/'tests/mods/startup_driver.c',ROOT/'tests/mods/platform_stub.c',ROOT/'runtime/mods.c',ROOT/'runtime/mod_package.c',ROOT/'tests/mods/runtime_test.c',ROOT/'tests/mods/settings_test.c',ROOT/'tests/mods/settings_io.c',*provider,*core,'-lm','-pthread','-o',build/'mod-startup-test'])
    run([sys.executable,'-B','-m','unittest','discover','-s','tests/mods','-p','test_*.py','-v'])
    all_lua=[p for p in sorted(lua.glob('*.c')) if p.name!='luac.c']
    run([*common,*all_lua,'-lm','-o',build/('lua-regressions'+extension)])
    run([build/('lua-regressions'+extension),ROOT/'tests/mods/dependency-regressions.lua'])

if __name__=='__main__': main()
