/* SPDX-License-Identifier: GPL-2.0-only
 * Early initramfs PID 1: load modules, mount disk root, reclaim RAM, exec SysVinit.
 * Never creates a filesystem, writes a partition table, or starts a root shell.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/magic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void stop(const char *why)
{
    perror(why);
    fprintf(stderr, "eeLinux: boot stopped; check root device, modules and console log.\n");
    for (;;) pause(); /* PID 1 must never exit. */
}

static unsigned int modules(void)
{
    char line[PATH_MAX];
    unsigned int loaded = 0;
    FILE *f = fopen("/etc/eelinux-modules", "r");
    if (!f) stop("/etc/eelinux-modules");
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        if (line[0] != '/' || strstr(line, "..")) { errno = EINVAL; stop("module path"); }
        int fd = open(line, O_RDONLY | O_CLOEXEC);
        if (fd < 0) stop(line);
        fprintf(stderr, "eeLinux: loading module %s\n", line);
        long rc = syscall(SYS_finit_module, fd, "", 0);
        if (rc < 0 && errno != EEXIST) stop(line);
        fprintf(stderr, "eeLinux: loaded module %s\n", line);
        loaded++;
        close(fd);
    }
    if (ferror(f)) stop("reading early module manifest");
    fclose(f);
    return loaded;
}

static void root_device(char *device, size_t size)
{
    FILE *f = fopen("/etc/eelinux-root", "r");
    if (!f || !fgets(device, size, f)) { errno = EINVAL; stop("/etc/eelinux-root"); }
    fclose(f);
    device[strcspn(device, "\r\n")] = 0;
    f = fopen("/proc/cmdline", "r");
    if (f) {
        char line[4096];
        if (fgets(line, sizeof(line), f)) {
            char *save, *p = strtok_r(line, " \t\r\n", &save);
            for (; p; p = strtok_r(NULL, " \t\r\n", &save))
                if (!strncmp(p, "eelinux.root=", sizeof("eelinux.root=") - 1)) {
                    const char *value = p + sizeof("eelinux.root=") - 1;
                    if (strlen(value) >= size) { errno = EINVAL; stop("root device too long"); }
                    strcpy(device, value);
                }
        }
        fclose(f);
    }
    if (strncmp(device, "/dev/", 5) || strstr(device, "..")) { errno = EINVAL; stop("root device"); }
}

/* GNU files live directly in ISO9660 with Rock Ridge permissions/symlinks.
 * Keep the read-only filesystem on disc and put writes in bounded tmpfs. */
static void dvd_root(const char *device)
{
    mkdir("/dvd",0755); mkdir("/dvd/disc",0755);
    mkdir("/dvd/rw",0755);
    if (mount(device,"/dvd/disc","iso9660",MS_RDONLY|MS_NOATIME,NULL)) stop("DVD ISO9660");
    struct stat root_stat;
    if (stat("/dvd/disc/eelinux/root",&root_stat) || !S_ISDIR(root_stat.st_mode))
        { errno=EINVAL; stop("DVD Rock Ridge GNU root"); }
    if (mount("tmpfs","/dvd/rw","tmpfs",MS_NOSUID|MS_NODEV,"size=8M,mode=0755")) stop("DVD writable session RAM");
    mkdir("/dvd/rw/upper",0755); mkdir("/dvd/rw/work",0755);
    if (mount("overlay","/newroot","overlay",0,
        "lowerdir=/dvd/disc/eelinux/root,upperdir=/dvd/rw/upper,workdir=/dvd/rw/work,index=off,metacopy=off")) stop("DVD overlay root");
    mkdir("/newroot/.dvd",0755); mkdir("/newroot/.dvd/disc",0755);
    mkdir("/newroot/.dvd/rw",0755);
    if (mount("/dvd/disc","/newroot/.dvd/disc",NULL,MS_MOVE,NULL) ||
        mount("/dvd/rw","/newroot/.dvd/rw",NULL,MS_MOVE,NULL)) stop("preserve DVD backing mounts");
    int marker=open("/newroot/.dvd/session",O_CREAT|O_WRONLY|O_CLOEXEC,0644);
    if (marker<0) stop("DVD session marker");
    close(marker);
    fprintf(stderr,"eeLinux: native ISO9660 GNU root mounted; 8 MiB writable RAM session; no USB root required\n");
}

/* Do not follow symlinks or cross into the disk/dev/proc/sys mounts while freeing initramfs. */
static void reclaim(const char *path, dev_t original)
{
    struct stat st;
    if (lstat(path, &st) || st.st_dev != original) return;
    if (!S_ISDIR(st.st_mode)) { if (unlink(path)) stop("reclaim initramfs file"); return; }
    DIR *dir = opendir(path);
    if (!dir) stop("reclaim initramfs directory");
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        char child[PATH_MAX];
        if (snprintf(child, sizeof(child), "%s/%s", path, ent->d_name) >= (int)sizeof(child))
            { errno = ENAMETOOLONG; stop("reclaim path"); }
        reclaim(child, original);
    }
    closedir(dir);
    if (strcmp(path, "/") && rmdir(path) && errno != EBUSY) stop("reclaim directory");
}

int main(void)
{
    if (getpid() != 1) { fprintf(stderr, "eeLinux initramfs program must run as PID 1\n"); return 1; }
    struct statfs initial_fs;
    if (statfs("/", &initial_fs)) stop("initial root filesystem");
    if ((unsigned int)initial_fs.f_type != RAMFS_MAGIC &&
        (unsigned int)initial_fs.f_type != TMPFS_MAGIC) {
        errno = EINVAL;
        stop("PID 1 requires a reclaimable ramfs/tmpfs initial root");
    }
    umask(022);
    mkdir("/dev", 0755); mkdir("/proc", 0755); mkdir("/sys", 0755); mkdir("/newroot", 0755);
    if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) && errno != EBUSY) stop("devtmpfs");
    int console = open("/dev/console", O_RDWR);
    if (console >= 0) { for (int i = 0; i < 3; i++) dup2(console, i); if (console > 2) close(console); }
    if (mount("proc", "/proc", "proc", 0, NULL)) stop("proc");
    if (mount("sysfs", "/sys", "sysfs", 0, NULL)) stop("sysfs");
    if (!modules())
        fprintf(stderr, "eeLinux: boot USB/storage/input drivers are built into the kernel\n");
    char device[PATH_MAX]; root_device(device, sizeof(device));
    int mounted = 0;
    int mount_errno = ENODEV;
    fprintf(stderr, "eeLinux: waiting for %s (up to 60 attempts)\n", device);
    for (int i = 0; i < 60; i++) {
        struct stat root_node;
        if (!stat(device, &root_node)) {
            if (!S_ISBLK(root_node.st_mode)) { errno = ENOTBLK; stop(device); }
            if (!strcmp(device,"/dev/ps2dvd")) { dvd_root(device); mounted=1; break; }
            if (!mount(device, "/newroot", "ext4", MS_RDONLY, NULL)) { mounted = 1; break; }
        }
        mount_errno = errno;
        if (i % 10 == 0)
            fprintf(stderr, "eeLinux: root device %s not ready: %s\n", device, strerror(mount_errno));
        sleep(1);
    }
    if (!mounted) { errno = mount_errno; stop(device); }
    fprintf(stderr, "eeLinux: mounted disk root %s\n", device);
    /* Absolute symlinks must resolve against the disk's root, not initramfs. */
    pid_t checker = fork();
    if (checker < 0) stop("fork disk root check");
    if (!checker) {
        if (chroot("/newroot") || chdir("/") ||
            access("/sbin/init", X_OK) || access("/bin/bash", X_OK)) _exit(1);
        _exit(0);
    }
    int checked;
    if (waitpid(checker, &checked, 0) != checker || !WIFEXITED(checked) || WEXITSTATUS(checked))
        { errno = EINVAL; stop("GNU disk root incomplete"); }
    if (chdir("/newroot")) stop("chdir newroot");
    mkdir("dev", 0755); mkdir("proc", 0755); mkdir("sys", 0755);
    if (mount("/dev", "dev", NULL, MS_MOVE, NULL) ||
        mount("/proc", "proc", NULL, MS_MOVE, NULL) ||
        mount("/sys", "sys", NULL, MS_MOVE, NULL)) stop("move API mounts");
    struct stat oldroot;
    if (stat("/", &oldroot)) stop("oldroot stat");
    reclaim("/", oldroot.st_dev);
    if (mount(".", "/", NULL, MS_MOVE, NULL) || chroot(".") || chdir("/")) stop("switch root");
    char build_id[128];
    FILE *build_file = fopen("/etc/eelinux-build-id", "r");
    if (build_file) {
        if (fgets(build_id, sizeof(build_id), build_file)) {
            build_id[strcspn(build_id, "\r\n")] = 0;
            fprintf(stderr, "eeLinux: disk build %s\n", build_id);
        }
        fclose(build_file);
    }
    fprintf(stderr, "eeLinux: starting GNU SysVinit /sbin/init\n");
    char *argv[] = { "/sbin/init", NULL };
    char *env[] = { "PATH=/sbin:/usr/sbin:/bin:/usr/bin", "HOME=/", "TERM=linux", NULL };
    execve(argv[0], argv, env);
    stop("exec GNU system init");
    return 1;
}
