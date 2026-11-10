#pragma once
#include <stddef.h>
static int mod_text_valid(const char *text,size_t n) {
    const unsigned char *s=(const unsigned char *)text;
    for(size_t i=0;i<n;){unsigned c=s[i++];if(c<32||c==127)return 0;if(c>=128){unsigned code;int more;
        if(c>=0xc2&&c<=0xdf){code=c&31;more=1;}else if(c>=0xe0&&c<=0xef){code=c&15;more=2;}else if(c>=0xf0&&c<=0xf4){code=c&7;more=3;}else return 0;
        int width=more;while(more--){if(i>=n||(s[i]&0xc0)!=0x80)return 0;code=(code<<6)|(s[i++]&63);}
        if((width==2&&code<0x800)||(width==3&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return 0;
    }}return 1;
}
