"""Build inert providers and standalone hosts; run native tests without game data/windows."""
from pathlib import Path
import argparse,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sanitize',action='store_true');p.add_argument('--cc',default='clang');p.add_argument('--cxx',default='clang++');a=p.parse_args()
BUILD=ROOT/'build/provider'/('sanitize' if a.sanitize else 'regular');BUILD.mkdir(parents=True,exist_ok=True)
windows=sys.platform=='win32';ext='.exe' if windows else '';libext='.dll' if windows else '.so'
flags=['-O1','-g','-Wall','-Wextra','-I'+str(ROOT/'runtime')]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
def run(cmd):subprocess.run(list(map(str,cmd)),check=True,cwd=ROOT,env=env)
for compiler,lang,std in [(a.cc,'c','c11'),(a.cxx,'c++','c++17')]:run([compiler,*flags,'-x',lang,'-std='+std,'-fsyntax-only',ROOT/'tests/providers/layout.c'])
libflags=['-shared','-fvisibility=hidden']+([] if windows else ['-fPIC'])
for mode in range(16):run([a.cc,*flags,'-std=gnu11',*libflags,'-DMODE='+str(mode),ROOT/'tests/providers/fixture_c.c','-o',BUILD/('c'+str(mode)+libext)])
run([a.cc,*flags,'-std=gnu11',*libflags,'-DMISSING',ROOT/'tests/providers/fixture_c.c','-o',BUILD/('missing'+libext)])
run([a.cc,*flags,'-std=gnu11',*libflags,'-DBUILD="c2"',ROOT/'tests/providers/fixture_c.c','-o',BUILD/('update'+libext)])
run([a.cxx,*flags,'-std=c++17','-fno-exceptions','-fno-rtti',*([] if windows else ['-fPIC']),'-c',ROOT/'tests/providers/fixture_cpp.cpp','-o',BUILD/'cpp.o'])
run([a.cc,*flags,*libflags,BUILD/'cpp.o','-o',BUILD/('cpp'+libext)])
# A forbidden package-private dependency must be rejected before provider constructors.
(BUILD/'dependency.c').write_text('int package_dependency(void){return 1;}\n')
run([a.cc,*libflags,BUILD/'dependency.c','-o',BUILD/('package_dep'+libext)])
if not windows:
 run([a.cc,*flags,*libflags,ROOT/'tests/providers/fixture_c.c','-Wl,--no-as-needed',str(BUILD/('package_dep'+libext)),'-o',BUILD/('dependency'+libext)])
if not windows:
 run([a.cc,*flags,*libflags,ROOT/'tests/providers/fixture_c.c','-Wl,-rpath,$ORIGIN','-o',BUILD/('rpath'+libext)])
run([a.cc,*flags,'-std=gnu11',ROOT/'tests/providers/abi_probe.c',ROOT/'runtime/mod_provider_platform.c',*([] if windows else ['-ldl']),'-o',BUILD/('abi-probe'+ext)])
source=[ROOT/'runtime'/f for f in ['mod_menu.c','mod_provider.c','mod_provider_platform.c','mod_package.c','mod_settings.c','sha256.c']]
if windows:
 # Standalone persistence adapter is unnecessary: linker discards unused save code.
 (BUILD/'io.c').write_text('#include <stddef.h>\nvoid plat_file_mkdir_p(const char *p){(void)p;} int plat_file_write_atomic(const char*p,const void*d,size_t n){(void)p;(void)d;(void)n;return 0;}\n')
 io=BUILD/'io.c'
else:io=ROOT/'tests/mods/settings_io.c'
run([a.cc,*flags,'-std=gnu11','-DXMP_STOP_TIMEOUT_MS=100',ROOT/'tests/providers/driver.c',*source,io,*([] if windows else ['-ldl']),'-o',BUILD/('host'+ext)])
# Real Lua runtime integration proves native retirement before shutdown/close, including init OOM.
lua=ROOT/'build/lua-src'
if not (lua/'lua.h').exists():
 sys.path.insert(0,str(ROOT/'tools'));from prepare_lua import prepare;lua=prepare()
core=[x for x in sorted(lua.glob('*.c')) if x.name not in ('lua.c','luac.c','linit.c') and (not x.name.endswith('lib.c') or x.name in ('lauxlib.c','lbaselib.c','lstrlib.c','ltablib.c','lmathlib.c'))]
run([a.cc,*flags,'-std=gnu11','-DXCX_MOD_TESTING','-DXMP_STOP_TIMEOUT_MS=100','-I'+str(lua),ROOT/'tests/providers/lua_driver.c',ROOT/'runtime/mod_runtime.c',*source,io,*core,*([] if windows else ['-ldl','-lm']),'-o',BUILD/('lua-host'+ext)])
if not windows:
 run([a.cc,*flags,'-std=gnu11','-DXCX_MOD_TESTING','-DXMP_STOP_TIMEOUT_MS=100','-I'+str(lua),ROOT/'tests/mods/lifecycle_driver.c',ROOT/'tests/mods/platform_stub.c',ROOT/'runtime/mods.c',ROOT/'runtime/mod_runtime.c',*source,io,*core,'-pthread','-ldl','-lm','-o',BUILD/'coordinator-host'])
env['PROVIDER_BUILD']=str(BUILD);env['PYTHONDONTWRITEBYTECODE']='1'
run([sys.executable,'-m','unittest','discover','-s','tests/providers','-p','test_*.py','-v'])
