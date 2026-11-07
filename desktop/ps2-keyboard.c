/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_keyboard_config.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int execute(char *const argv[])
{
    pid_t child=fork();if(child<0){perror("keyboard");return -1;}
    if(!child){execv(argv[0],argv);perror(argv[0]);_exit(127);}
    int status;while(waitpid(child,&status,0)<0)if(errno!=EINTR)return -1;
    return WIFEXITED(status)?WEXITSTATUS(status):-1;
}
static int console(const struct ps2_keyboard_layout *layout)
{
    char path[128];struct stat st;
    snprintf(path,sizeof(path),"/usr/share/keymaps/%s",layout->map);
    if(stat(path,&st) || !S_ISREG(st.st_mode)){fprintf(stderr,"Keyboard map unavailable: %s\n",path);return -1;}
    char *args[]={"/usr/bin/loadkeys","--quiet","--unicode","--console","/dev/tty1",path,NULL};
    return execute(args);
}
static int xorg(const struct ps2_keyboard_layout *layout)
{
    if(!getenv("DISPLAY") || !*getenv("DISPLAY"))return 0;
    char *args[]={"/usr/bin/setxkbmap","-layout",(char *)layout->xkb,"-variant",(char *)layout->variant,
        "-model",(char *)layout->model,"-option","",NULL};
    return execute(args);
}
static int save(const struct ps2_keyboard_layout *layout)
{
    char temporary[]=PS2_KEYBOARD_SETTINGS ".XXXXXXXX";
    int fd=mkstemp(temporary);if(fd<0)return -1;
    FILE *out=fdopen(fd,"w");if(!out){close(fd);unlink(temporary);return -1;}
    int okay=fchmod(fd,0644)==0 && fprintf(out,"# Selected with ps2-keyboard\nLAYOUT=%s\n",layout->id)>0;
    if(fflush(out) || fsync(fd))okay=0;
    if(fclose(out))okay=0;
    if(okay && !rename(temporary,PS2_KEYBOARD_SETTINGS))return 0;
    int error=errno;unlink(temporary);errno=error;return -1;
}
int main(int argc,char **argv)
{
    struct ps2_keyboard_layout current,chosen;
    if(ps2_keyboard_current(NULL,&current)){perror("Keyboard settings");return 1;}
    if(argc==2 && !strcmp(argv[1],"--get")){printf("%s\t%s\n",current.id,current.label);return 0;}
    if(argc==2 && !strcmp(argv[1],"--apply-console"))return console(&current)?1:0;
    struct ps2_keyboard_layout layouts[PS2_KEYBOARD_LAYOUT_LIMIT];
    int count=ps2_keyboard_layouts(layouts,PS2_KEYBOARD_LAYOUT_LIMIT);
    if(count<=0){perror("Keyboard layouts");return 1;}
    if(argc==2 && !strcmp(argv[1],"--list")){
        for(int i=0;i<count;++i)printf("%-12s %s\n",layouts[i].id,layouts[i].label);
        return 0;
    }
    const char *id=NULL;char answer[64];
    if(argc==3 && !strcmp(argv[1],"--set"))id=argv[2];
    else if(argc==1){
        printf("Keyboard layout: %s\n\n",current.label);
        for(int i=0;i<count;++i)printf("%2d. %-12s %s%s\n",i+1,layouts[i].id,layouts[i].label,
            strcmp(layouts[i].id,current.id)?"":" (selected)");
        printf("\nChoose a number or layout ID (q to cancel): ");fflush(stdout);
        if(!fgets(answer,sizeof(answer),stdin))return 0;
        answer[strcspn(answer,"\r\n")]=0;
        if(!*answer || !strcmp(answer,"q"))return 0;
        char *end;long n=strtol(answer,&end,10);
        id=(*answer && !*end && n>0 && n<=count)?layouts[n-1].id:answer;
    }else{fprintf(stderr,"Usage: ps2-keyboard [--list | --get | --set ID | --apply-console]\n");return 2;}
    if(ps2_keyboard_current(id,&chosen)){fprintf(stderr,"Unknown keyboard layout: %s\n",id);return 2;}
    if(geteuid()){fprintf(stderr,"Run as root to save the console and Xorg keyboard layout.\n");return 1;}
    if(console(&chosen))return 1;
    if(xorg(&chosen) || save(&chosen)){
        perror("Applying keyboard layout");console(&current);xorg(&current);return 1;
    }
    printf("Keyboard layout saved: %s\n",chosen.label);
    return 0;
}
