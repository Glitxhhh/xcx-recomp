// Synthetic descriptor fixtures and real generated selection/text functions, without game descriptor bytes.
#define _GNU_SOURCE
#include "host.h"
#include "mod_menu.h"
#include "mod_settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
void f_700083aa70(Cpu *);void f_700066bee8(Cpu *);
unsigned guard=1,feedback,transition,raw_calls,message_calls,shown;
volatile int g_fnprof;const char rt_game_name[]="2.0.0";
static uint8_t object[0x1600],other[0x1600];static uint64_t block;
void fnprof_hit(uint64_t a,uint64_t b,uint64_t c){(void)a;(void)b;(void)c;}
void rt_dispatch(Cpu *c){(void)c;abort();}
void rt_log(const char *s,...){(void)s;}
uint32_t mem_query(uint64_t p,RtMemoryInfo *i){i->base=p;i->size=0x100000;i->perm=3;return 0;}
void *mem_kernel_alloc(uint64_t n,uint64_t align){(void)n;(void)align;assert(!block);block=0x7010000000;assert(mmap((void*)block,4096,3,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void*)block);return (void*)block;}
static void page(uint64_t p,size_t n){assert(mmap((void*)p,n,3,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void*)p);}
static void initial(uint8_t *o){ST64((uint64_t)o+0x10c0,0x7002763140);ST32((uint64_t)o+0x10c8,8);}
static void selected(uint8_t *o){uint64_t c=(uint64_t)o;ST32(c+0x10b0,3);ST32(c+0xda8,1);ST32(c+0x1108,5);ST32(c+0x1190,5);ST32(c+0x1188,6);ST32(c+0x1160+5*4,5);ST64(c+0x1198,block+8*0x30+5*0x30);}
static void publish(int state){ModView v={0};strcpy(v.id,"alpha");strcpy(v.page_id,"preferences");v.status=state;mod_ui_publish(&v);}
static void cpu(Cpu *c,uint64_t o,uint8_t *stack){*c=(Cpu){0};c->x[0]=o;c->sp=(uint64_t)(stack+4096);c->x[30]=0x1234;for(int i=19;i<30;++i)c->x[i]=0x123400+i;}
int main(void){page(0x7002762000,8192);page(0x7001d57000,4096);uint8_t flag=1;ST64(0x7001d57590,(uint64_t)&flag);
 uint64_t parents=0x7002763140,online=parents+5*0x30,children=0x7002762e10;for(int i=0;i<8*0x30;++i)((uint8_t*)parents)[i]=(uint8_t)i;for(int i=0;i<5*0x30;++i)((uint8_t*)children)[i]=(uint8_t)(i+7);
 ST64(online,0x7000838fcc);ST64(online+8,0);ST16(online+0x10,0x1256);ST64(online+0x18,children);ST32(online+0x20,5);ST8(online+0x28,0);
 uint64_t cb[]={0x838324,0x838404,0x83851c,0x838604,0x8386e4};unsigned ids[]={0x120d,0x14cf,0x120b,0x3bf,0x14d0};for(int i=0;i<5;++i){ST64(children+i*0x30,0x7000000000+cb[i]);ST64(children+i*0x30+8,0);ST16(children+i*0x30+0x10,ids[i]);}
 uint8_t original[8*0x30];memcpy(original,(void*)parents,sizeof original);mod_ui_reset("");mod_menu_reset();initial(object);rt_mod_menu_install((uint64_t)object);assert(LD64((uint64_t)object+0x10c0)==parents);assert(!block);
 mod_menu_support(1);ModMenuEntry e={0},got;strcpy(e.owner,"alpha");strcpy(e.entry,"entry");strcpy(e.page,"preferences");strcpy(e.label,"Original label");assert(!mod_menu_register(&e));assert(!strcmp(mod_menu_register(&e),"full"));publish(MOD_LOADED);rt_mod_menu_install((uint64_t)object);assert(block);assert(LD32(block+5*0x30+0x20)==6);assert(!memcmp((void*)(block+8*0x30),(void*)children,5*0x30));for(int i=0;i<8;++i)if(i!=5)assert(!memcmp((void*)(block+i*0x30),original+i*0x30,0x30));assert(!memcmp((void*)parents,original,sizeof original));
 initial(other);rt_mod_menu_install((uint64_t)other);assert(LD64((uint64_t)other+0x10c0)==block);selected(object);
 uint8_t stack[4096] __attribute__((aligned(16)));for(int mode=0;mode<4;++mode){Cpu c;cpu(&c,(uint64_t)object,stack);Cpu before=c;selected(object);guard=mode!=1;feedback=transition=0;if(mode==2)ST64((uint64_t)object+0x1198,children);if(mode==3)publish(MOD_FAULTED);f_700083aa70(&c);assert(c.x[0]==1&&c.sp==before.sp&&c.x[30]==before.x[30]);for(int i=19;i<30;++i)assert(c.x[i]==before.x[i]);assert(feedback==(unsigned)(mode==2)&&transition==(unsigned)(mode==2));if(mode==0){assert(mod_menu_take(&got));assert(!strcmp(got.owner,"alpha"));}else assert(!mod_menu_take(&got));}
 publish(MOD_LOADED);selected(object);uint64_t view=(uint64_t)object+0x98;ST64(view+0x680+5*0xb8+0x10,0xabc);uint64_t message[2]={1,0x1256};for(unsigned refresh=0;refresh<3;++refresh){Cpu c;cpu(&c,view,stack);c.x[1]=1;c.x[2]=5;c.x[3]=(uint64_t)message;if(refresh==0){ST32((uint64_t)object+0x1188,5);ST32((uint64_t)object+0x1160+5*4,0);c.x[3]=rt_mod_menu_message(view,1,block+5*0x30+0x18,5*0x30,(uint64_t)message);assert(c.x[3]!=(uint64_t)message);}else selected(object);Cpu before=c;f_700066bee8(&c);assert(c.sp==before.sp&&c.x[30]==before.x[30]);for(int i=19;i<30;++i)assert(c.x[i]==before.x[i]);}assert(raw_calls==3&&message_calls==0&&shown>=3);
 ST64(view+0x680+0x10,0xdef);Cpu c;cpu(&c,view,stack);c.x[1]=1;c.x[2]=0;c.x[3]=(uint64_t)message;f_700066bee8(&c);assert(message_calls==1&&raw_calls==3);
 assert(mod_menu_request());assert(!mod_menu_request());rt_mod_menu_invalidate();assert(!mod_menu_take(&got));publish(MOD_FAULTED);initial(other);rt_mod_menu_install((uint64_t)other);assert(LD64((uint64_t)other+0x10c0)==parents);mod_menu_support(0);assert(!mod_menu_snapshot(&got));
 puts("menu adapter: disabled five rows, immutable six, originals intact, selection guards, owner fault, labels/refresh, cancellation, X19-X30/SP passed");
}
