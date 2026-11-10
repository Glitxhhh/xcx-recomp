// One checked XCX DE 2.0.0 Online-child adapter; all guest access is synchronous on its original thread.
#include "host.h"
#include "mod_menu.h"
#include <stdatomic.h>
#if __has_include("mod_menu_config.h")
#include "mod_menu_config.h"
#else
#define MOD_MENU_HOOKS 0
#endif
extern const char rt_game_name[];
#define MAIN 0x7000000000ull
static atomic_flag init_lock=ATOMIC_FLAG_INIT;
static atomic_uintptr_t storage;
// Eight parent descriptors, six Online children and a copied ASCII caption, process lifetime.
#define PARENTS_SIZE (8*0x30)
#define CHILDREN_SIZE (6*0x30)
#define BLOCK_SIZE (PARENTS_SIZE+CHILDREN_SIZE+64)
int rt_mod_menu_capable(void){return MOD_MENU_HOOKS&&!strcmp(rt_game_name,"2.0.0");}
static int readable(uint64_t p,uint64_t n){RtMemoryInfo i;return p&&n&&p<=UINT64_MAX-n&&!mem_query(p,&i)&&p>=i.base&&p-i.base<=i.size&&n<=i.size-(p-i.base)&&(i.perm&1);}
static int installed(uint64_t c,uint64_t block){return block&&readable(c,0x1568)&&LD64(c+0x10c0)==block&&LD32(c+0x10c8)==8;}
void rt_mod_menu_install(uint64_t c){
 mod_menu_invalidate();ModMenuEntry e;if(!rt_mod_menu_capable()||!mod_menu_snapshot(&e)||!readable(c,0x1568))return;
 if(atomic_flag_test_and_set_explicit(&init_lock,memory_order_acquire))return;
 uint64_t b=atomic_load_explicit(&storage,memory_order_acquire);
 if(!b){
  uint64_t parents=LD64(c+0x10c0),online=MAIN+0x2763230,children=MAIN+0x2762e10;
  const uint64_t cb[]={0x838324,0x838404,0x83851c,0x838604,0x8386e4};const unsigned ids[]={0x120d,0x14cf,0x120b,0x3bf,0x14d0};
  int ok=LD32(c+0x10c8)==8&&parents+5*0x30==online&&readable(parents,PARENTS_SIZE)&&readable(children,5*0x30)&&LD64(online)==MAIN+0x838fcc&&LD64(online+8)==0&&LD16(online+0x10)==0x1256&&LD64(online+0x18)==children&&LD32(online+0x20)==5&&LD8(online+0x28)==0;
  for(int j=0;j<5&&ok;++j)ok=LD64(children+j*0x30)==MAIN+cb[j]&&LD64(children+j*0x30+8)==0&&LD16(children+j*0x30+0x10)==ids[j];
  if(ok){void *owned=mem_kernel_alloc(BLOCK_SIZE,16);if(owned){b=(uint64_t)(uintptr_t)owned;memcpy(owned,(void*)(uintptr_t)parents,PARENTS_SIZE);memcpy((char*)owned+PARENTS_SIZE,(void*)(uintptr_t)children,5*0x30);uint64_t row=b+PARENTS_SIZE+5*0x30;ST64(row,MAIN+0x838fcc);ST16(row+0x10,0x1256);ST64(b+5*0x30+0x18,b+PARENTS_SIZE);ST32(b+5*0x30+0x20,6);memcpy((void*)(uintptr_t)(b+PARENTS_SIZE+CHILDREN_SIZE),e.label,strlen(e.label)+1);ST64(b+PARENTS_SIZE+CHILDREN_SIZE+48,1);atomic_store_explicit(&storage,(uintptr_t)b,memory_order_release);rt_log("mods menu: owned Online child descriptors ready");}}
 }
 if(b){ST64(c+0x10c0,b);rt_log("mods menu: installed sixth Online row");}
 atomic_flag_clear_explicit(&init_lock,memory_order_release);
}
uint64_t rt_mod_menu_message(uint64_t view,uint64_t level,uint64_t span,uint64_t offset,uint64_t original){
 uint64_t b=atomic_load_explicit(&storage,memory_order_acquire);
 if(level==1&&installed(view-0x98,b)&&LD32(view-0x98+0x1108)==5&&span==b+5*0x30+0x18&&offset==5*0x30)return b+PARENTS_SIZE+CHILDREN_SIZE+48;return original;
}
uint64_t rt_mod_menu_label(uint64_t view,uint64_t level,uint64_t visible,uint64_t message){
 uint64_t b=atomic_load_explicit(&storage,memory_order_acquire),c=view-0x98;
 if(level!=1||visible>=9||!installed(c,b)||LD32(c+0x1108)!=5)return 0;
 if(message!=b+PARENTS_SIZE+CHILDREN_SIZE+48&&(visible>=LD32(c+0x1188)||LD32(c+0x1160+visible*4)!=5))return 0;
 return b+PARENTS_SIZE+CHILDREN_SIZE;
}
int rt_mod_menu_select(uint64_t c){
 uint64_t b=atomic_load_explicit(&storage,memory_order_acquire);if(!installed(c,b)||LD32(c+0x10b0)!=3||LD32(c+0xda8)!=1||LD32(c+0x1108)!=5||LD64(c+0x1198)!=b+PARENTS_SIZE+5*0x30)return 0;
 unsigned visible=LD32(c+0x1190);if(visible>=9||visible>=LD32(c+0x1188)||LD32(c+0x1160+visible*4)!=5)return 0;
 // Installed synthetic rows never fall into the original guest action lifecycle, even after owner loss.
 int accepted=mod_menu_request();rt_log("mods menu: requested registered page (%s)",accepted?"accepted":"unavailable");return 1;
}
void rt_mod_menu_invalidate(void){mod_menu_invalidate();}
