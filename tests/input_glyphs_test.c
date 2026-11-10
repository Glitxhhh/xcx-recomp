// Pure policy and pre-merge intent tests; no window, controller, guest bytes or simulated gameplay input.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "input_glyphs.h"
#include "host.h"
const RtFileHash rt_exefs_sha256[]={{"main","0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026"}};
const int rt_exefs_sha256_count=1;
static const char *pref="auto", *variant="standard";
const char *cfg_value(const char *name){return !strcmp(name,"XCX_GLYPH_APPEARANCE") ? variant : pref;}
const char *cfg_get(const char *name){(void)name;return NULL;}
void rt_log(const char *fmt,...){(void)fmt;}
static unsigned runtime_note_calls;
static const char *runtime_note;
const char *appendage_glyph_runtime_note(void){runtime_note_calls++;return runtime_note_calls==1 ? runtime_note : NULL;}
void input_glyph_snapshot(int *s,int *f,int *l,InputMap *m){*s=GLYPH_SOURCE_KEYBOARD;*f=GLYPH_PAD_PS;*l=PAD_LAYOUT_XBOX;*m=input_map;}
static void sample_unchanged(GlyphIntent *state, const GlyphIntentSample *sample) {
    GlyphIntentSample copy = *sample; glyph_intent_sample(state,sample); assert(!memcmp(&copy,sample,sizeof copy));
}
static unsigned checks;
#define CHECK(x) do {assert(x);checks++;} while(0)
int main(void){
 input_map_reset(); InputMap original=input_map; GlyphDecision d;
 for(int layout=0;layout<PAD_LAYOUT_COUNT;layout++)for(int style=GLYPH_PLAYSTATION;style<=GLYPH_KEYBOARD;style++){
  d=glyph_decide(style,GLYPH_SOURCE_NONE,GLYPH_PAD_UNKNOWN,layout,&input_map);CHECK(d.style==style);
  CHECK(d.art[0]==glyph_art_find(style,style==GLYPH_KEYBOARD?KEY_R:PAD_R));
  CHECK(d.art[1]==glyph_art_find(style,style==GLYPH_KEYBOARD?KEY_MOUSE_L:layout==PAD_LAYOUT_XBOX?PAD_NORTH:PAD_WEST));
  CHECK(d.art[2]==glyph_art_find(style,style==GLYPH_KEYBOARD?KEY_E:layout==PAD_LAYOUT_XBOX?PAD_SOUTH:PAD_EAST));
 }
 d=glyph_decide(GLYPH_AUTO,GLYPH_SOURCE_KEYBOARD,GLYPH_PAD_PS,1,&input_map);CHECK(d.style==GLYPH_KEYBOARD);
 const int styles[]={GLYPH_ORIGINAL,GLYPH_ORIGINAL,GLYPH_PLAYSTATION,GLYPH_XBOX,GLYPH_STEAM};
 for(int f=0;f<5;f++)CHECK(glyph_decide(GLYPH_AUTO,GLYPH_SOURCE_PAD,f,1,&input_map).style==styles[f]);
 CHECK(glyph_decide(GLYPH_AUTO,GLYPH_SOURCE_NONE,GLYPH_PAD_PS,1,&input_map).style==GLYPH_ORIGINAL);
 CHECK(glyph_decide(GLYPH_PLAYSTATION,GLYPH_SOURCE_KEYBOARD,GLYPH_PAD_PS,1,&input_map).style==GLYPH_PLAYSTATION);
 CHECK(glyph_decide(GLYPH_PLAYSTATION,GLYPH_SOURCE_PAD,GLYPH_PAD_PS,-1,&input_map).reason);
 InputMap m=input_map;m.nkeys=0;CHECK(glyph_decide(GLYPH_KEYBOARD,1,2,1,&m).reason);
 m=input_map;m.keys[m.nkeys++]=(Binding){KEY_T,B_R};CHECK(glyph_decide(GLYPH_KEYBOARD,1,2,1,&m).reason);
 m=input_map;m.keys[m.nkeys++]=(Binding){KEY_R,B_A};CHECK(glyph_decide(GLYPH_KEYBOARD,1,2,1,&m).reason);
 m=input_map;for(int i=0;i<m.nkeys;i++)if(m.keys[i].action==B_R)m.keys[i].key=KEY_F12;
 CHECK(glyph_decide(GLYPH_KEYBOARD,1,2,1,&m).reason);
 m=input_map;for(int i=0;i<m.nkeys;i++)if(m.keys[i].action==B_R)m.keys[i].key=KEY_T;
 CHECK(glyph_decide(GLYPH_KEYBOARD,1,2,1,&m).art[0]==glyph_art_find(GLYPH_KEYBOARD,KEY_T));
 m=input_map;m.pads[1][m.npads[1]++]=(PadBinding){PAD_R,B_A};CHECK(glyph_decide(GLYPH_XBOX,2,3,1,&m).reason);
 CHECK(!memcmp(&input_map,&original,sizeof original));CHECK(glyph_build_supported());
 CHECK(glyph_style_parse("bogus")==GLYPH_ORIGINAL);
 // Fresh input wins; held input, drift, reconnect/focus-held baselines and single motion spikes do not steal.
 GlyphIntent state={0};GlyphIntentSample p={.active=1,.captured=1,.device=1,.now=1000000000};
 sample_unchanged(&state,&p);CHECK(state.source==0);
 p.keys[KEY_R]=1;p.now+=1000000;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.buttons=1u<<PAD_R;p.now+=1000000;sample_unchanged(&state,&p);CHECK(state.source==2);
 p.now+=1000000;sample_unchanged(&state,&p);CHECK(state.source==2);
 p.keys[KEY_R]=0;p.keys[KEY_E]=1;p.now+=1000000;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.axes[0]=6000;p.now+=100000000;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.axes[0]=23000;p.now+=100000000;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.now+=41000000;sample_unchanged(&state,&p);CHECK(state.source==2);
 p.keys[KEY_T]=1;p.now+=1000000;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.now+=500000000;sample_unchanged(&state,&p);CHECK(state.source==1); // held stick/controller cannot steal back
 p.active=0;p.now++;sample_unchanged(&state,&p);p.active=1;p.now++;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.device=2;p.now++;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.buttons=0;p.axes[0]=0;p.now++;sample_unchanged(&state,&p);
 p.buttons=1u<<PAD_R;p.now++;sample_unchanged(&state,&p);CHECK(state.source==2);
 p.device=0;p.now++;sample_unchanged(&state,&p);CHECK(state.source==0);
 p.device=3;p.now++;sample_unchanged(&state,&p);CHECK(state.source==0); // reconnect-held ignored
 p.buttons=0;p.now++;sample_unchanged(&state,&p);p.buttons=1u<<PAD_R;p.now++;sample_unchanged(&state,&p);CHECK(state.source==2);
 p.now+=500000000;p.mouse_x=1000;sample_unchanged(&state,&p);CHECK(state.source==2);
 p.mouse_x=1;p.now+=200000000;sample_unchanged(&state,&p);CHECK(state.source==2);
 p.mouse_x=18;p.now+=200000000;sample_unchanged(&state,&p);p.now+=40000000;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.axes[4]=-32768;p.now+=100000000;sample_unchanged(&state,&p);p.now+=100000000;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.mouse_x=0;p.axes[0]=-25000;p.now+=1000000;sample_unchanged(&state,&p);p.now+=41000000;sample_unchanged(&state,&p);CHECK(state.source==2);
 p.keys[KEY_U]=1;p.now++;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.axes[0]=25000;p.now++;sample_unchanged(&state,&p);CHECK(state.source==1);
 p.now+=41000000;sample_unchanged(&state,&p);CHECK(state.source==2);
 // Manual selection never mutates the remembered source; returning to auto uses the last intentional source.
 pref="playstation";CHECK(glyph_current_decision().style==GLYPH_PLAYSTATION);pref="auto";CHECK(glyph_current_decision().style==GLYPH_KEYBOARD);
 runtime_note="Original virtual labels: glyph history exhausted; restart game to restore styles";
 CHECK(strstr(glyph_status(),"history exhausted; restart"));CHECK(runtime_note_calls==1);
 // The new weapon guide reports its own unsupported remap even if Appendage is supported.
 runtime_note=NULL;
 for(int i=0;i<input_map.nkeys;i++)if(input_map.keys[i].action==B_X)input_map.keys[i].key=KEY_F12;
 CHECK(strstr(glyph_status(),"weapon: Original virtual labels"));input_map=original;
 CHECK(glyph_decide_button(GLYPH_KEYBOARD,GLYPH_SOURCE_KEYBOARD,GLYPH_PAD_PS,1,&input_map,B_X).art[0]==glyph_art_find(GLYPH_KEYBOARD,KEY_SPACE));
 CHECK(glyph_art_find(GLYPH_KEYBOARD,KEY_LSHIFT)>=GLYPH_ART_MAX);   // Shift: a tile past the 128 the HUD route can address
 { int16_t sym[GLYPH_SYMBOLS]; glyph_decide_symbol_set(GLYPH_KEYBOARD,GLYPH_SOURCE_KEYBOARD,GLYPH_PAD_PS,1,&input_map,GLYPH_STANDARD,sym); CHECK(sym[SYM_B]==glyph_art_find(GLYPH_KEYBOARD,KEY_LSHIFT)); }   // B on Shift
 CHECK(glyph_decide_button(GLYPH_XBOX,GLYPH_SOURCE_PAD,GLYPH_PAD_XBOX,1,&input_map,B_A).art[0]==glyph_art_find(GLYPH_XBOX,PAD_SOUTH));
 CHECK(glyph_decide_button(GLYPH_XBOX,GLYPH_SOURCE_PAD,GLYPH_PAD_XBOX,1,&input_map,B_B).style==GLYPH_ORIGINAL);
 CHECK(glyph_style_parse("switch")==GLYPH_ORIGINAL);
 CHECK(glyph_decide(GLYPH_ORIGINAL,GLYPH_SOURCE_KEYBOARD,GLYPH_PAD_PS,-1,&m).style==GLYPH_ORIGINAL);
 CHECK(glyph_decide(GLYPH_ORIGINAL,GLYPH_SOURCE_PAD,GLYPH_PAD_SWITCH,1,&m).art[0]==-1);
 CHECK(glyph_decide(GLYPH_ORIGINAL,GLYPH_SOURCE_PAD,GLYPH_PAD_PS,1,&m).art[0]==-1);
 CHECK(glyph_appearance_parse("colored")==GLYPH_COLORED);CHECK(glyph_appearance_parse("unknown")==GLYPH_STANDARD);
 for(int style=GLYPH_PLAYSTATION;style<=GLYPH_STEAM;style++) {
  for(int physical=PAD_SOUTH;physical<=PAD_NORTH;physical++) {
   int standard=glyph_art_find(style,physical), colored=glyph_art_variant(standard,GLYPH_COLORED);
   CHECK(colored>=63 && colored<75);CHECK(colored!=standard);CHECK(glyph_art_variant(standard,GLYPH_STANDARD)==standard);
   CHECK(strstr(glyph_art_name(colored),"color"));
  }
  CHECK(glyph_art_variant(glyph_art_find(style,PAD_R),GLYPH_COLORED)==glyph_art_find(style,PAD_R));
 }
 pref="playstation";variant="colored";d=glyph_current_decision();
 CHECK(d.art[0]==glyph_art_find(GLYPH_PLAYSTATION,PAD_R));CHECK(strstr(glyph_art_name(d.art[1]),"color"));
 CHECK(strstr(glyph_art_name(glyph_current_button(B_X).art[0]),"playstation_west_colored"));
 pref="auto";CHECK(glyph_current_button(B_X).art[0]==glyph_art_find(GLYPH_KEYBOARD,KEY_SPACE));
 pref="original";CHECK(glyph_current_button(B_X).art[0]==-1);CHECK(strstr(glyph_status(),"Original"));
 pref="auto";variant="standard";
 for(unsigned art=0;art<glyph_art_count();art++){uint32_t pixels[512*192];CHECK(glyph_art_pixels(art,pixels));
  for(int y=0;y<64;y++){CHECK(!memcmp(pixels+(y+64)*512+448,pixels+y*512+320,256));CHECK(!memcmp(pixels+y*512+320,pixels+y*512+384,256));CHECK(!memcmp(pixels+y*512+384,pixels+(y+64)*512+384,256));}
 }
 printf("%u policy, action/binding, intent, reconnect/focus/held, pointer and licensed tile checks passed\n",checks);
}
