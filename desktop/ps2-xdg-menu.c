/* SPDX-License-Identifier: MIT
 * Small XDG desktop-entry to TWM menu adapter. Never evaluates Exec as shell
 * syntax: desktop arguments are parsed, expanded and individually shell quoted.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* A warm menu needs metadata checks, rather than opening every desktop entry.
 * Track absent paths too: newly installed programs and higher-priority entries
 * must invalidate the cache just as edits, chmod and removals do. The cache is
 * optional; a missing, unwritable, incompatible or truncated file is harmless. */
#define CACHE_LIMIT 8192
struct file_state {
    uint64_t device,inode,size;
    int64_t modified,changed;
    uint32_t mode,uid,gid,modified_ns,changed_ns,exists;
};
struct dependency {char *path;struct file_state state;};
static struct dependency *dependencies;
static unsigned dependency_count,dependency_capacity;
static int cache_enabled;
static FILE *menu_output;
static struct file_state state(const char *path)
{
    struct file_state result={0};struct stat info;
    if(!stat(path,&info)){
        result.device=info.st_dev;result.inode=info.st_ino;result.size=info.st_size;
        result.modified=info.st_mtim.tv_sec;result.modified_ns=info.st_mtim.tv_nsec;
        result.changed=info.st_ctim.tv_sec;result.changed_ns=info.st_ctim.tv_nsec;
        result.mode=info.st_mode;result.uid=info.st_uid;result.gid=info.st_gid;result.exists=1;
    }
    return result;
}
static void depend(const char *path)
{
    if(!cache_enabled)return;
    if(dependency_count==CACHE_LIMIT){cache_enabled=0;return;}
    if(dependency_count==dependency_capacity){
        unsigned capacity=dependency_capacity?dependency_capacity*2:64;
        struct dependency *grown=realloc(dependencies,capacity*sizeof(*grown));
        if(!grown){cache_enabled=0;return;}
        dependencies=grown;dependency_capacity=capacity;
    }
    char *name=strdup(path);
    if(!name){cache_enabled=0;return;}
    dependencies[dependency_count++]=(struct dependency){name,state(path)};
}
struct cache_header {char magic[8];uint64_t digest;uint32_t context,dependencies,menu,reserved;};
static uint64_t menu_digest(const char *menu,size_t size)
{
    uint64_t digest=UINT64_C(14695981039346656037);
    for(size_t i=0;i<size;++i){digest^=(unsigned char)menu[i];digest*=UINT64_C(1099511628211);}
    return digest;
}
static char *cache_context(int argc,char **argv,size_t *length)
{
    char *data=NULL;FILE *out=open_memstream(&data,length);
    if(!out)return NULL;
    const char *names[]={"HOME","XDG_DATA_HOME","XDG_DATA_DIRS","LC_ALL","LC_MESSAGES","LANG","PATH"};
    fprintf(out,"menu-cache-2:%lu:%lu:%lu:%lu\n",(unsigned long)getuid(),
            (unsigned long)geteuid(),(unsigned long)getgid(),(unsigned long)getegid());
    int n=getgroups(0,NULL);gid_t *groups=n>0?malloc((size_t)n*sizeof(*groups)):NULL;
    if(n<0 || (n>0 && (!groups || getgroups(n,groups)!=n))){free(groups);fclose(out);free(data);return NULL;}
    for(int i=0;i<n;++i)fprintf(out,"%lu,",(unsigned long)groups[i]);
    free(groups);fputc('\n',out);
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i){
        const char *value=getenv(names[i]);if(!value)value="";
        fprintf(out,"%zu:",strlen(value));fputs(value,out);
    }
    for(int i=1;i<argc;++i)fprintf(out,"%zu:%s",strlen(argv[i]),argv[i]);
    if(fclose(out)){free(data);return NULL;}
    return data;
}
static int cached_menu(const char *path,const char *context,size_t context_size)
{
    FILE *in=fopen(path,"rb");if(!in)return 0;
    struct cache_header header;char *stored=NULL,*menu=NULL;int valid=0;
    if(fread(&header,sizeof(header),1,in)!=1 || memcmp(header.magic,"TWMENU2",8) || header.reserved ||
       header.context!=context_size || header.dependencies>CACHE_LIMIT ||
       !header.menu || header.menu>1024*1024)goto done;
    stored=malloc(header.context?header.context:1);
    if(!stored || fread(stored,1,header.context,in)!=header.context ||
       memcmp(stored,context,context_size))goto done;
    for(unsigned i=0;i<header.dependencies;++i){
        uint32_t size;char name[4096];struct file_state saved,current;
        if(fread(&size,sizeof(size),1,in)!=1 || !size || size>=sizeof(name) ||
           fread(name,1,size,in)!=size || memchr(name,0,size) ||
           fread(&saved,sizeof(saved),1,in)!=1)goto done;
        name[size]=0;current=state(name);
        if(memcmp(&saved,&current,sizeof(saved)))goto done;
    }
    menu=malloc(header.menu);
    if(!menu || fread(menu,1,header.menu,in)!=header.menu || fgetc(in)!=EOF ||
       ferror(in) || memchr(menu,0,header.menu) || menu_digest(menu,header.menu)!=header.digest)goto done;
    valid=fwrite(menu,1,header.menu,stdout)==header.menu;
done:
    free(stored);free(menu);fclose(in);return valid;
}
static void save_menu(const char *path,const char *context,size_t context_size,
                      const char *menu,size_t menu_size)
{
    if(!cache_enabled || context_size>65536 || menu_size>1024*1024)return;
    char *temporary=malloc(strlen(path)+16);if(!temporary)return;
    sprintf(temporary,"%s.XXXXXXXX",path);int fd=mkstemp(temporary);
    if(fd<0){free(temporary);return;}
    FILE *out=fdopen(fd,"wb");if(!out){close(fd);unlink(temporary);free(temporary);return;}
    struct cache_header header={{'T','W','M','E','N','U','2',0},menu_digest(menu,menu_size),context_size,dependency_count,menu_size,0};
    int ok=fwrite(&header,sizeof(header),1,out)==1 && fwrite(context,1,context_size,out)==context_size;
    for(unsigned i=0;ok && i<dependency_count;++i){
        struct dependency *d=&dependencies[i];uint32_t size=strlen(d->path);
        struct file_state current=state(d->path);
        /* Do not publish a snapshot that changed while it was being parsed. */
        ok=!memcmp(&d->state,&current,sizeof(current)) && fwrite(&size,sizeof(size),1,out)==1 &&
           fwrite(d->path,1,size,out)==size && fwrite(&d->state,sizeof(d->state),1,out)==1;
    }
    if(ok)ok=fwrite(menu,1,menu_size,out)==menu_size;
    if(fclose(out))ok=0;
    if(ok)rename(temporary,path);
    unlink(temporary);free(temporary);
}
static void menu_line(const char *text){fputs(text,menu_output);fputc('\n',menu_output);}

struct entry {
    char *id,*file,*name,*exec,*tryexec,*icon,*path,*only,*exclude,*category;
    int hidden,terminal,application,priority;
};
static struct entry *entries[2048];
static unsigned count;
static const char *locale_name;
static char language[16];
static char *copy(const char *s){char *p=strdup(s);if(!p){perror("strdup");exit(1);}return p;}
static void replace(char **p,const char *s){free(*p);*p=copy(s);}
static int yes(const char *s){return !strcasecmp(s,"true");}
static void unescape(char *s)
{
    char *d=s;
    while(*s){
        if(*s=='\\' && s[1]){
            ++s;switch(*s){case 's':*d++=' ';break;case 'n':*d++='\n';break;
            case 't':*d++='\t';break;case 'r':*d++='\r';break;
            case '\\':*d++='\\';break;default:*d++='\\';*d++=*s;break;}++s;
        }else *d++=*s++;
    }*d=0;
}
static int list_has(const char *s,const char *name)
{
    if(!s)return 0;
    size_t n=strlen(name);
    while(*s){const char *end=strchr(s,';');size_t len=end?(size_t)(end-s):strlen(s);
        if(n==len && !strncmp(s,name,n))return 1;
        if(!end)break;
        s=end+1;
    }return 0;
}
static int executable(const char *s)
{
    if(!s || !*s)return 0;
    if(strchr(s,'/')){depend(s);return !access(s,X_OK);}
    const char *path=getenv("PATH");char *dirs=copy(path?path:"/usr/bin:/bin"),*save=NULL;
    int found=0;
    for(char *d=strtok_r(dirs,":",&save);d;d=strtok_r(NULL,":",&save)){
        char file[4096];if(snprintf(file,sizeof(file),"%s/%s",d,s)<(int)sizeof(file)){
            depend(file);if(!access(file,X_OK)){found=1;break;}
        }
    }free(dirs);return found;
}
static void read_entry(const char *file,const char *id)
{
    for(unsigned i=0;i<count;++i)if(!strcmp(entries[i]->id,id))return;
    if(count==2048)return;
    depend(file);
    FILE *f=fopen(file,"r");if(!f)return;
    struct entry *e=calloc(1,sizeof(*e));if(!e)exit(1);
    e->id=copy(id);e->file=copy(file);entries[count++]=e;
    char line[8192];int section=0;
    while(fgets(line,sizeof(line),f)){
        line[strcspn(line,"\r\n")]=0;
        if(line[0]=='['){section=!strcmp(line,"[Desktop Entry]");continue;}
        if(!section || line[0]=='#')continue;
        char *equal=strchr(line,'=');if(!equal)continue;*equal++=0;unescape(equal);
        if(!strcmp(line,"Type"))e->application=!strcmp(equal,"Application");
        else if(!strcmp(line,"Name")){if(!e->priority)replace(&e->name,equal);}
        else if(!strncmp(line,"Name[",5)){
            char *end=strchr(line+5,']');if(!end)continue;*end=0;
            int rank=!strcmp(line+5,locale_name)?2:!strcmp(line+5,language)?1:0;
            if(rank && rank>=e->priority){replace(&e->name,equal);e->priority=rank;}
        }
        else if(!strcmp(line,"Exec"))replace(&e->exec,equal);
        else if(!strcmp(line,"TryExec"))replace(&e->tryexec,equal);
        else if(!strcmp(line,"Icon"))replace(&e->icon,equal);
        else if(!strcmp(line,"Path"))replace(&e->path,equal);
        else if(!strcmp(line,"OnlyShowIn"))replace(&e->only,equal);
        else if(!strcmp(line,"NotShowIn"))replace(&e->exclude,equal);
        else if(!strcmp(line,"X-PS2-Menu"))replace(&e->category,equal);
        else if(!strcmp(line,"Hidden") || !strcmp(line,"NoDisplay"))e->hidden|=yes(equal);
        else if(!strcmp(line,"Terminal"))e->terminal=yes(equal);
    }fclose(f);
}
static void scan(const char *folder,const char *prefix,unsigned depth)
{
    if(depth>8)return;
    depend(folder);
    DIR *d=opendir(folder);if(!d)return;struct dirent *item;
    while((item=readdir(d))){
        if(item->d_name[0]=='.')continue;
        char path[4096],id[4096];
        if(snprintf(path,sizeof(path),"%s/%s",folder,item->d_name)>=(int)sizeof(path) ||
           snprintf(id,sizeof(id),"%s%s",prefix,item->d_name)>=(int)sizeof(id))continue;
        size_t n=strlen(id);
        if(n>8 && !strcmp(id+n-8,".desktop"))read_entry(path,id);
        else if(depth<8){if(n+1>=sizeof(id))continue;id[n]='-';id[n+1]=0;scan(path,id,depth+1);}
    }closedir(d);
}
struct buffer{char data[32768];size_t used;int failed;};
static void append(struct buffer *b,const char *s)
{size_t n=strlen(s);if(n>=sizeof(b->data)-b->used){b->failed=1;return;}memcpy(b->data+b->used,s,n+1);b->used+=n;}
static void shell_arg(struct buffer *b,const char *s)
{
    append(b," '");while(*s){char c[2]={*s++,0};append(b,c[0]=='\''?"'\\''":c);}append(b,"'");
}
static void twm_string(const char *s)
{
    fputc('"',menu_output);while(*s){unsigned char c=*s++;if(c=='"' || c=='\\')fputc('\\',menu_output);
        if(c=='\n' || c=='\r' || c=='\t')fputc(' ',menu_output);else if(c>=32)fputc(c,menu_output);
    }fputc('"',menu_output);
}
static int command(struct entry *e,struct buffer *b)
{
    if(!e->application || e->hidden || !e->name || !*e->name || !e->exec ||
       (e->tryexec && !executable(e->tryexec)) ||
       (e->only && !list_has(e->only,"TWM") && !list_has(e->only,"PS2")) ||
       list_has(e->exclude,"TWM") || list_has(e->exclude,"PS2"))return 0;
    if(e->path && *e->path){append(b,"cd");shell_arg(b,e->path);append(b," &&");}
    if(e->terminal){append(b," xterm -hold -title");shell_arg(b,e->name);append(b," -e");}
    const char *s=e->exec;unsigned argc=0;
    while(*s){
        while(isspace((unsigned char)*s))++s;
        if(!*s)break;
        struct buffer token={0};int quoted=0,started=0;
        while(*s && (quoted || !isspace((unsigned char)*s))){
            char c=*s++;started=1;
            if(c=='"'){quoted=!quoted;continue;}
            if(c=='\\'){if(!*s)return 0;c=*s++;}
            char p[2]={c,0};append(&token,p);
        }
        if(quoted || token.failed || !started)return 0;
        if(!strcmp(token.data,"%f") || !strcmp(token.data,"%F") ||
           !strcmp(token.data,"%u") || !strcmp(token.data,"%U"))continue;
        if(!strcmp(token.data,"%i")){if(e->icon){shell_arg(b,"--icon");shell_arg(b,e->icon);}continue;}
        struct buffer expanded={0};
        for(const char *p=token.data;*p;++p){
            if(*p=='%'){
                ++p;if(!*p)return 0;
                if(*p=='c')append(&expanded,e->name);
                else if(*p=='k')append(&expanded,e->file);
                else if(*p=='%')append(&expanded,"%");
                else return 0;
            }else{char one[2]={*p,0};append(&expanded,one);}
        }
        if(expanded.failed || (!argc && !executable(expanded.data)))return 0;
        shell_arg(b,expanded.data);if(++argc>128)return 0;
    }
    append(b," &");return argc && !b->failed;
}
static int compare(const void *a,const void *b)
{
    const struct entry *x=*(struct entry *const *)a,*y=*(struct entry *const *)b;
    return strcasecmp(x->name?x->name:x->id,y->name?y->name:y->id);
}
int main(int argc,char **argv)
{
    const char *cache=NULL;char *context=NULL,*menu=NULL;size_t context_size=0,menu_size=0;
    menu_output=stdout;
    if(argc>=3 && !strcmp(argv[1],"--cache")){
        cache=argv[2];argc-=2;argv+=2;
        context=cache_context(argc,argv,&context_size);
        if(context && context_size<=65536){
            if(cached_menu(cache,context,context_size)){free(context);return ferror(stdout)?1:0;}
            menu_output=open_memstream(&menu,&menu_size);
            if(menu_output){cache_enabled=1;depend("/proc/self/exe");}
            else menu_output=stdout;
        }
    }
    const char *lang=getenv("LC_ALL");if(!lang || !*lang)lang=getenv("LC_MESSAGES");if(!lang || !*lang)lang=getenv("LANG");
    char local[128];snprintf(local,sizeof(local),"%s",lang?lang:"C");local[strcspn(local,".")]=0;locale_name=local;
    snprintf(language,sizeof(language),"%.15s",local);language[strcspn(language,"_@")]=0;
    if(argc>1){for(int i=1;i<argc;++i)scan(argv[i],"",0);}
    else{
        char path[4096];const char *home=getenv("HOME"),*data=getenv("XDG_DATA_HOME");
        if(data && *data)snprintf(path,sizeof(path),"%s/applications",data);
        else snprintf(path,sizeof(path),"%s/.local/share/applications",home?home:"/root");
        scan(path,"",0);
        const char *dirs=getenv("XDG_DATA_DIRS");char *paths=copy(dirs && *dirs?dirs:"/usr/local/share:/usr/share"),*save=NULL;
        for(char *d=strtok_r(paths,":",&save);d;d=strtok_r(NULL,":",&save)){
            if(*d!='/' || snprintf(path,sizeof(path),"%s/applications",d)>=(int)sizeof(path))continue;
            scan(path,"",0);
        }free(paths);
    }
    qsort(entries,count,sizeof(entries[0]),compare);
    menu_line("menu \"applications\" {\n \"Applications\" f.title\n \"Accessories\" f.menu \"xdg-accessories\"\n \"Applets\" f.menu \"xdg-applets\"\n \"Tools\" f.menu \"xdg-tools\"\n}");
    const char *groups[]={"accessories","applets","tools"};
    for(unsigned group=0;group<3;++group){
        fprintf(menu_output,"menu \"xdg-%s\" {\n",groups[group]);unsigned shown=0;
        for(unsigned i=0;i<count;++i){
            struct entry *e=entries[i];unsigned category=e->terminal?2:e->category && !strcmp(e->category,"Applets")?1:0;
            struct buffer b={0};if(category!=group || !command(e,&b))continue;
            if(shown && shown%20==0){
                fprintf(menu_output," \"More applications\" f.menu \"xdg-%s-%u\"\n}\nmenu \"xdg-%s-%u\" {\n",
                    groups[group],shown/20,groups[group],shown/20);
            }
            fputc(' ',menu_output);twm_string(e->name);fputs(" f.exec ",menu_output);twm_string(b.data);menu_line("");++shown;
        }
        if(!shown)menu_line(" \"No applications installed\" f.nop");
        menu_line("}");
    }
    if(menu_output!=stdout){
        if(fclose(menu_output)){free(context);free(menu);return 1;}
        if(fwrite(menu,1,menu_size,stdout)!=menu_size){free(context);free(menu);return 1;}
        save_menu(cache,context,context_size,menu,menu_size);
        free(menu);
    }
    free(context);
    return ferror(stdout)?1:0;
}
