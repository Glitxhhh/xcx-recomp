"""Build/test service ABI 2.0, Lua dispatch and inert fixtures without a game window."""
from pathlib import Path
import argparse,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--cc',default='clang');p.add_argument('--cxx',default='clang++');a=p.parse_args();WIN=sys.platform=='win32';EXT='.dll' if WIN else '.so';EXE='.exe' if WIN else ''
BUILD=ROOT/'build/actions'/('sanitize' if a.sanitize else 'regular');BUILD.mkdir(parents=True,exist_ok=True)
flags=['-O1','-g','-Wall','-Wextra','-I'+str(ROOT/'runtime')]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
def run(cmd):subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,check=True)
for cc,lang,std in [(a.cc,'c','c11'),(a.cxx,'c++','c++17')]:run([cc,*flags,'-x',lang,'-std='+std,'-fsyntax-only',ROOT/'tests/actions/layout.c'])
lib=['-shared','-fvisibility=hidden']+([] if WIN else ['-fPIC'])
for mode in range(8):run([a.cc,*flags,'-std=gnu11',*lib,'-DMODE='+str(mode),ROOT/'tests/actions/service_c.c','-o',BUILD/('c'+str(mode)+EXT)])
run([a.cc,*flags,'-std=gnu11',*lib,'-DBUILD="work2"',ROOT/'tests/actions/service_c.c','-o',BUILD/('update'+EXT)])
run([a.cxx,*flags,'-std=c++17','-fno-exceptions','-fno-rtti',*([] if WIN else ['-fPIC']),'-c',ROOT/'tests/actions/service_cpp.cpp','-o',BUILD/'cpp.o']);run([a.cc,*flags,*lib,BUILD/'cpp.o','-o',BUILD/('cpp'+EXT)])
run([a.cc,*flags,'-std=gnu11',ROOT/'tests/actions/abi_probe.c',ROOT/'runtime/mod_provider_platform.c',*([] if WIN else ['-ldl']),'-o',BUILD/('abi-probe'+EXE)])
source=[ROOT/'runtime'/n for n in ['mod_menu.c','mod_provider.c','mod_provider_platform.c','mod_package.c','mod_settings.c','sha256.c']]
if WIN:
 (BUILD/'io.c').write_text('#include <stddef.h>\nvoid plat_file_mkdir_p(const char*p){(void)p;} int plat_file_write_atomic(const char*p,const void*d,size_t n){(void)p;(void)d;(void)n;return 0;}\n');io=BUILD/'io.c'
else:io=ROOT/'tests/mods/settings_io.c'
run([a.cc,*flags,'-std=gnu11','-DXMP_STOP_TIMEOUT_MS=100',ROOT/'tests/actions/native_driver.c',*source,io,*([] if WIN else ['-ldl']),'-o',BUILD/('native'+EXE)])
lua=ROOT/'build/lua-src'
if not (lua/'lua.h').exists():
 sys.path.insert(0,str(ROOT/'tools'));from prepare_lua import prepare;lua=prepare()
core=[x for x in sorted(lua.glob('*.c')) if x.name not in ('lua.c','luac.c','linit.c') and (not x.name.endswith('lib.c') or x.name in ('lauxlib.c','lbaselib.c','lstrlib.c','ltablib.c','lmathlib.c'))]
run([a.cc,*flags,'-std=gnu11','-DXCX_MOD_TESTING','-DXMP_STOP_TIMEOUT_MS=100','-I'+str(lua),ROOT/'tests/actions/lua_driver.c',ROOT/'runtime/mod_runtime.c',*source,io,*core,*([] if WIN else ['-ldl','-lm']),'-o',BUILD/('lua'+EXE)])
env['ACTIONS_BUILD']=str(BUILD);run([sys.executable,'-B','-m','unittest','discover','-s','tests/actions','-p','test_*.py','-v'])
