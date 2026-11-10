"""Emit and execute checked menu paths using synthetic runtime fixtures; generated C stays ignored."""
from pathlib import Path
import argparse,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'recomp'));import gen
from module import Module
import mod_menu,patches
from layout import layout
p=argparse.ArgumentParser();p.add_argument('--exefs',type=Path,required=True);a=p.parse_args();out=ROOT/'build/menu/contract';out.mkdir(parents=True,exist_ok=True);m=Module('main',a.exefs/'main',0x7000000000);mod_menu.validate(m,(a.exefs/"main").read_bytes());mod_menu.validate_patches(patches.load(ROOT/"recomp/patches-upd.txt",layout(a.exefs)));starts=set((m.lo+gen.discover(m)[0]*4).tolist());e=gen.Emitter(m,starts,mod_menu_hook=True);body=[];refs=set()
for start,end in [(0x83aa70,0x83ad20),(0x66bee8,0x66bf80)]:
 b,r=e.function(0x7000000000+start,0x7000000000+end);body+=b;refs|=r
(out/'paths.c').write_text('\n'.join([gen.header('menu contract'),'#include "cpu.h"','#include "simd.h"',*[f'void f_{x:x}(Cpu *);' for x in sorted(refs)],*body])+'\n')
stubs=['#include "cpu.h"','#include <stdlib.h>','#include <string.h>','#include <assert.h>','extern unsigned guard,feedback,transition,raw_calls,message_calls,shown;']
for x in sorted(refs):
 code={0x700040b93c:'c->x[0]=4;',0x700040ba84:'c->x[0]=0;',0x70005f2aa4:'c->x[0]=2;',0x700083b998:'c->x[0]=guard;',0x700040bc48:'++feedback;',0x700066c27c:'++transition;',0x7000560464:'++shown;',0x7000917160:'++message_calls;',0x700091720c:'assert(!strcmp((const char*)c->x[1],"Original label"));++raw_calls;for(int i=0;i<19;++i)c->x[i]=i;'}.get(x,'abort();')
 stubs.append(f'void f_{x:x}(Cpu *c){{(void)c;{code}}}')
(out/'stubs.c').write_text('\n'.join(stubs)+'\n');(out/'mod_menu_config.h').write_text('#define MOD_MENU_HOOKS 1\n')
subprocess.run(['clang','-std=gnu11','-O1','-g','-I'+str(out),'-Iruntime',out/'paths.c',out/'stubs.c','tests/menu/adapter.c','runtime/mod_menu_adapter.c','runtime/mod_menu.c','runtime/mod_settings.c','tests/mods/settings_io.c','-o',out/'test'],cwd=ROOT,check=True);subprocess.run([out/'test'],check=True)
