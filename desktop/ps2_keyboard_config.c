/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_keyboard_config.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int field(char *out,size_t size,const char *in,int label)
{
    size_t n=strlen(in);
    if(!n || n>=size || (!label && strstr(in,"..")))return -1;
    for(size_t i=0;i<n;++i)if(label?((unsigned char)in[i]<32 || (unsigned char)in[i]>126):
        !(isalnum((unsigned char)in[i]) || in[i]=='-' || in[i]=='_' || in[i]=='.'))return -1;
    memcpy(out,in,n+1);return 0;
}
int ps2_keyboard_layouts(struct ps2_keyboard_layout *layouts,size_t capacity)
{
    FILE *f=fopen(PS2_KEYBOARD_CATALOG,"r");
    if(!f)return -1;
    char line[512];size_t count=0;int error=EINVAL;
    while(fgets(line,sizeof(line),f)){
        if(!strchr(line,'\n') && !feof(f))goto fail;
        line[strcspn(line,"\r\n")]=0;
        if(!*line || *line=='#')continue;
        if(count==capacity){error=E2BIG;goto fail;}
        char *parts[6],*p=line;
        for(unsigned i=0;i<6;++i){
            parts[i]=p;
            char *tab=strchr(p,'\t');
            if(i<5){if(!tab)goto fail;*tab=0;p=tab+1;}
            else if(tab)goto fail;
        }
        struct ps2_keyboard_layout *k=&layouts[count];
        if(field(k->id,sizeof(k->id),parts[0],0) || field(k->xkb,sizeof(k->xkb),parts[1],0) ||
           field(k->variant,sizeof(k->variant),parts[2],0) || field(k->model,sizeof(k->model),parts[3],0) ||
           field(k->map,sizeof(k->map),parts[4],0) || field(k->label,sizeof(k->label),parts[5],1))goto fail;
        if(!strcmp(k->variant,"-"))k->variant[0]=0;
        for(size_t i=0;i<count;++i)if(!strcmp(layouts[i].id,k->id))goto fail;
        ++count;
    }
    if(ferror(f))goto fail;
    fclose(f);return (int)count;
fail:
    fclose(f);errno=error;return -1;
}
int ps2_keyboard_current(const char *requested,struct ps2_keyboard_layout *selected)
{
    char id[32]="us";
    if(requested){if(field(id,sizeof(id),requested,0)){errno=EINVAL;return -1;}}
    else{
        FILE *f=fopen(PS2_KEYBOARD_SETTINGS,"r");
        if(f){
            char line[128];
            while(fgets(line,sizeof(line),f)){
                if(strncmp(line,"LAYOUT=",7))continue;
                char *value=line+7;value[strcspn(value,"\r\n")]=0;
                size_t n=strlen(value);
                if(n>=2 && ((value[0]=='\"' && value[n-1]=='\"') || (value[0]=='\'' && value[n-1]=='\''))){
                    value[n-1]=0;++value;
                }
                if(field(id,sizeof(id),value,0)){fclose(f);errno=EINVAL;return -1;}
            }
            int bad=ferror(f);fclose(f);if(bad)return -1;
        }else if(errno!=ENOENT)return -1;
    }
    struct ps2_keyboard_layout layouts[PS2_KEYBOARD_LAYOUT_LIMIT];
    int count=ps2_keyboard_layouts(layouts,PS2_KEYBOARD_LAYOUT_LIMIT);
    if(count<0){
        if(errno!=ENOENT || strcmp(id,"us"))return -1;
        *selected=(struct ps2_keyboard_layout){"us","us","","pc104","eelinux-us.map","English US / ANSI"};
        return 0;
    }
    for(int i=0;i<count;++i)if(!strcmp(layouts[i].id,id)){*selected=layouts[i];return 0;}
    errno=EINVAL;return -1;
}
