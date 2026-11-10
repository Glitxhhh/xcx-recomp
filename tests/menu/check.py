"""Production Lua menu registration and bounded registry tests, no game window."""
from pathlib import Path
import argparse,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];WIN=sys.platform=='win32';EXE='.exe' if WIN else '';p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args();out=ROOT/'build/menu'/('sanitize' if a.sanitize else 'regular');out.mkdir(parents=True,exist_ok=True)
s=(ROOT/'tests/actions/lua_driver.c').read_text().replace('#include "mod_internal.h"','#include "mod_internal.h"\n#include "mod_menu.h"');s=s.replace('mod_ui_reset(argv[2]);','mod_ui_reset(argv[2]);mod_menu_reset();mod_menu_support(!getenv("MENU_TEST_UNSUPPORTED"));');s=s.replace('mod_ui_shutdown();mod_runtime_stop(rt);','ModMenuEntry e;if(mod_menu_snapshot(&e))printf("registered %s %s %s %s\\n",e.owner,e.entry,e.page,e.label);else puts("unregistered");mod_ui_shutdown();mod_runtime_stop(rt);');s=s.replace('ModRuntime *rt=','snprintf(plan->game_id,sizeof plan->game_id,"2.0.0");ModRuntime *rt=');(out/'driver.c').write_text(s)
flags=['-O1','-g','-std=gnu11','-DXCX_MOD_TESTING','-Iruntime','-Ibuild/lua-src']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
lua=ROOT/'build/lua-src';core=[x for x in sorted(lua.glob('*.c')) if x.name not in ('lua.c','luac.c','linit.c') and (not x.name.endswith('lib.c') or x.name in ('lauxlib.c','lbaselib.c','lstrlib.c','ltablib.c','lmathlib.c'))]
source=[ROOT/'runtime'/n for n in ['mod_menu.c','mod_runtime.c','mod_provider.c','mod_provider_platform.c','mod_package.c','mod_settings.c','sha256.c']];env=os.environ.copy();env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',MENU_TEST_EXE=str(out/('host'+EXE)))
io=ROOT/'tests/mods/settings_io.c'
if WIN:
 io=out/'io.c';io.write_text('#include <stddef.h>\nvoid plat_file_mkdir_p(const char*p){(void)p;} int plat_file_write_atomic(const char*p,const void*d,size_t n){(void)p;(void)d;(void)n;return 0;}\n')
subprocess.run(['clang',*flags,out/'driver.c',*source,io,*core,*([] if WIN else ['-ldl','-lm']),'-o',out/('host'+EXE)],cwd=ROOT,env=env,check=True);subprocess.run([sys.executable,'-B','-m','unittest','discover','-s','tests/menu','-p','test_*.py','-v'],cwd=ROOT,env=env,check=True)
