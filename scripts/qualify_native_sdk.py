"""Exercise stripped native tools in a disposable R5900 Linux-user chroot.

Requires root only for temporary binfmt registration/chroot. The registration
is removed in finally. No PS2 kernel, device, RAM or physical-boot claim.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,time

ROOT=Path(__file__).resolve().parents[1]

CHECK=r'''#!/bin/bash
set -euo pipefail
cd /var/tmp
export TMPDIR=/var/tmp TERM=xterm
gcc --version | head -1
g++ --version | head -1
ld --version | head -1
python3 --version
lynx -version | head -3
nano --version | head -2
vim --version | head -2
gdb --version | head -1
gcc -### -c -x c /dev/null 2> compiler-defaults.log
grep -- '-march=r5900' compiler-defaults.log
grep -- '-msoft-float' compiler-defaults.log
grep -- '-mfix-r5900' compiler-defaults.log
grep -- '-mno-llsc' compiler-defaults.log
grep -- '-EL' compiler-defaults.log
cat > hello.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
static _Atomic int counter;
static void *worker(void *arg) { (void)arg; atomic_fetch_add(&counter, 7); return NULL; }
int main(void) { pthread_t t; if (pthread_create(&t,NULL,worker,NULL)) return 2;
  if (pthread_join(t,NULL) || atomic_load(&counter)!=7) return 3;
  volatile double a=1.25, b=2.0; if (a*b!=2.5) return 4;
  puts("native-C-threads-atomics-softfloat-ok"); return EXIT_SUCCESS; }
EOF
cat > hello.cc <<'EOF'
#include <iostream>
#include <vector>
#include <numeric>
#include <stdexcept>
int main() { std::vector<int> v{1,2,3,4};
  if (std::accumulate(v.begin(),v.end(),0)!=10) return 1;
  try { throw std::runtime_error("unwind"); } catch (const std::runtime_error&) {}
  std::cout << "native-C++-vector-ok\n"; }
EOF
printf 'all:\n\tgcc -O2 -Wall -Wextra -Werror -pthread hello.c -o hello-c\n\tg++ -O2 -Wall -Wextra -Werror hello.cc -o hello-cxx\n\t./hello-c\n\t./hello-cxx\n' > Makefile
make -j1
gcc -O2 -static -pthread hello.c -o hello-c-static
g++ -O2 -static hello.cc -o hello-cxx-static
./hello-c-static
./hello-cxx-static
gcc -O2 -c hello.c -o hello.o
ar rcs libhello.a hello.o
ranlib libhello.a
nm -g libhello.a | grep ' main'
readelf -h hello-c | grep 'MIPS'
objdump -f hello-c | grep 'mips:5900'
printf '%%option noyywrap\n%%%%\n[0-9]+ { puts("native-flex-ok"); }\n. ;\n\\n ;\n%%%%\nint main(void) { return yylex(); }\n' > scanner.l
flex scanner.l
gcc -O2 lex.yy.c -o scanner
printf '42\n' | ./scanner
autoconf --version | head -1
automake --version | head -1
libtool --version | head -1
perl -e 'print "native-perl-ok\n"'
python3 - <<'EOF'
import os,sys,ssl,hashlib,ctypes,sqlite3,curses,readline,bz2,lzma,zlib,dbm.gnu
from compression import zstd
assert sys.version_info[:3] == tuple(map(int,os.environ['EELINUX_EXPECTED_PYTHON_VERSION'].split('.'))), sys.version
payload=b'PS2-native-python-stdlib'*128
for module in (bz2,lzma,zlib,zstd): assert module.decompress(module.compress(payload))==payload
assert sqlite3.connect(':memory:').execute('select 6*7').fetchone()[0]==42
assert ctypes.CDLL(None).getpid()>0
libm=ctypes.CDLL('libm.so.6');libm.cos.argtypes=[ctypes.c_double];libm.cos.restype=ctypes.c_double
assert abs(libm.cos(0.0)-1.0)<1e-12
callback=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_int)(lambda value:value+1)
assert callback(41)==42
assert hashlib.sha256(b'abc').hexdigest().startswith('ba7816bf')
assert ssl.create_default_context().cert_store_stats()['x509_ca']>0
readline.parse_and_bind('tab: complete')
print('native-python-SSL-SQLite-ctypes-readline-compression-ok')
EOF
cat > sqlite-sdk.c <<'EOF'
#include <stdio.h>
#include <sqlite3.h>
int main(void) { sqlite3 *db=NULL;
 if (sqlite3_libversion_number()!=SQLITE_VERSION_NUMBER) return 1;
 if (sqlite3_open(":memory:",&db)!=SQLITE_OK) return 2;
 if (sqlite3_exec(db,"create table test(value); insert into test values(42)",NULL,NULL,NULL)!=SQLITE_OK) return 3;
 sqlite3_close(db); puts("native-SQLite-matching-SDK-ok"); return 0; }
EOF
gcc -O1 sqlite-sdk.c -o sqlite-sdk $(pkgconf --cflags --libs sqlite3)
./sqlite-sdk
gcc -O1 -static sqlite-sdk.c -o sqlite-sdk-static $(pkgconf --cflags --libs --static sqlite3)
./sqlite-sdk-static
cat > skarnet-sdk.c <<'EOF'
#include <stdio.h>
#include <skalibs/netstring.h>
static int (*volatile api)(int,stralloc *,uint32_t *)=mininetstring_read;
int main(void) { if (!api) return 1; puts("native-Skarnet-SDK-ok"); return 0; }
EOF
gcc -O1 skarnet-sdk.c -o skarnet-sdk -lskarnet
./skarnet-sdk
cat > openssl-sdk.c <<'EOF'
#include <stdio.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
int main(void) { if (OPENSSL_VERSION_MAJOR!=OPENSSL_version_major()) return 1;
 printf("native-OpenSSL-%d-matching-SDK-ok\n",OPENSSL_VERSION_MAJOR); return 0; }
EOF
for package in openssl openssl3; do
 gcc -O1 openssl-sdk.c -o "$package-sdk" $(pkgconf --cflags --libs "$package")
 ./"$package-sdk"
 gcc -O1 -static openssl-sdk.c -o "$package-sdk-static" $(pkgconf --cflags --libs --static "$package")
 ./"$package-sdk-static"
done
python3 - <<'EOF'
import pathlib,shlex,subprocess,sysconfig
source=pathlib.Path('native_python_extension.c')
source.write_text("""#include <Python.h>
static PyObject *answer(PyObject *self,PyObject *unused) {(void)self;(void)unused;return PyLong_FromLong(42);}
static PyMethodDef methods[]={{"answer",answer,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};
static struct PyModuleDef module={PyModuleDef_HEAD_INIT,"native_python_extension",NULL,-1,methods};
PyMODINIT_FUNC PyInit_native_python_extension(void){return PyModule_Create(&module);}
""")
output=source.with_name(source.stem+sysconfig.get_config_var('EXT_SUFFIX'))
command=shlex.split(sysconfig.get_config_var('LDSHARED'))+shlex.split(sysconfig.get_config_var('CFLAGS'))
command+=['-fPIC','-I'+sysconfig.get_path('include'),str(source),'-o',str(output)]
subprocess.run(command,check=True)
import native_python_extension
assert native_python_extension.answer()==42
print('native-Python-extension-SDK-ok')
EOF
printf '<html><body>native-Lynx-ok</body></html>\n' > browser.html
lynx -dump -nolist browser.html | grep native-Lynx-ok
printf 'native-GNU-tools-ok\n' | gzip | gzip -d | grep native-GNU-tools-ok
printf 'native-sdk-completed-errors=0\n'
'''

# A bounded physical-memory check uses small translation units. The broader
# library/tool check above remains the Linux-user qualification.
SMALL_CHECK=r'''#!/bin/bash
set -euo pipefail
cd /var/tmp
export TMPDIR=/var/tmp TERM=xterm
gcc --version | head -1
g++ --version | head -1
ld --version | head -1
cat > hello.c <<'EOF'
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
static _Atomic int counter;
static void *worker(void *arg) { (void)arg; atomic_fetch_add(&counter,7); return NULL; }
int main(void) { pthread_t t; if (pthread_create(&t,NULL,worker,NULL)) return 2;
 if (pthread_join(t,NULL) || atomic_load(&counter)!=7) return 3;
 volatile double a=1.25,b=2.0; if (a*b!=2.5) return 4;
 puts("native-C-threads-atomics-softfloat-ok"); return 0; }
EOF
cat > hello.cc <<'EOF'
extern "C" int puts(const char *);
int main() { try { throw 42; } catch (int value) { if (value!=42) return 1; }
 puts("native-C++-exceptions-ok"); }
EOF
printf 'all:\n\tgcc -O1 -Wall -Wextra -Werror -pthread hello.c -o hello-c\n\t./hello-c\n\tg++ -O1 -Wall -Wextra -Werror hello.cc -o hello-cxx\n\t./hello-cxx\n' > Makefile
make -j1
gcc -O1 -static -pthread hello.c -o hello-c-static
./hello-c-static
g++ -O1 -static hello.cc -o hello-cxx-static
./hello-cxx-static
gcc -O1 -c hello.c -o hello.o
ar rcs libhello.a hello.o
ranlib libhello.a
nm -g libhello.a | grep ' main'
objdump -f hello-c | grep 'mips:5900'
printf 'native-sdk-completed-errors=0\n'
'''

def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('staging','work','qemu','report'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    if os.geteuid()!=0:raise RuntimeError('Run the disposable SDK qualification as root')
    source=args.staging.resolve(strict=True);work=args.work.resolve()
    if work.exists():raise RuntimeError('Use a new qualification work directory')
    work.mkdir(parents=True);tree=work/'root'
    subprocess.run(['cp','-a','--reflink=auto',str(source),str(tree)],check=True)
    # QEMU is a host executable. Its loader/libraries are copied only into this
    # disposable root under their x86_64 names, never the production stage.
    dependencies=subprocess.check_output(['ldd',str(args.qemu)],text=True)
    for filename in re.findall(r'(/[^\s()]+)',dependencies):
        path=Path(filename)
        if path.is_file():
            destination=tree/filename.lstrip('/');destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,destination)
    for name,major,minor in (('null',1,3),('zero',1,5),('random',1,8),('urandom',1,9)):
        destination=tree/'dev'/name;destination.parent.mkdir(exist_ok=True)
        if destination.exists() or destination.is_symlink():destination.unlink()
        os.mknod(destination,0o020666,os.makedev(major,minor))
    (tree/'var/tmp').mkdir(parents=True,exist_ok=True)
    (tree/'var/tmp').chmod(0o1777)
    check=tree/'native-sdk-check.sh';check.write_text(CHECK);check.chmod(0o755)
    name='eelinux-sdk-'+str(os.getpid());registration=Path('/proc/sys/fs/binfmt_misc')/name
    magic=b'\x7fELF\x01\x01\x01'+b'\0'*9+b'\x02\0\x08\0'
    mask=b'\xff'*7+b'\0'*9+b'\xfe\xff\xff\xff'
    escape=lambda value:''.join('\\x'+format(byte,'02x') for byte in value)
    rule=f':{name}:M::{escape(magic)}:{escape(mask)}:{args.qemu.resolve()}:F'
    registered=False;started=time.monotonic()
    try:
        Path('/proc/sys/fs/binfmt_misc/register').write_text(rule);registered=True
        environment={'PATH':'/usr/sbin:/usr/bin:/sbin:/bin','HOME':'/root','LC_ALL':'C',
            'QEMU_CPU':'R5900','TMPDIR':'/var/tmp','TERM':'xterm',
            'EELINUX_EXPECTED_PYTHON_VERSION':json.loads((ROOT/'configs/native-tools-latest.json').read_text())['python3']['version']}
        with (work/'runtime.log').open('w',buffering=1) as log:
            result=subprocess.run(['/usr/sbin/chroot',str(tree),'/bin/bash','/native-sdk-check.sh'],
                env=environment,stdout=log,stderr=subprocess.STDOUT,timeout=600)
        output=(work/'runtime.log').read_text()
        passed=result.returncode==0 and 'native-sdk-completed-errors=0' in output
        report={'result':'passed' if passed else 'failed','scope':'stripped native compiler/tool/stdlib execution on R5900 Linux-user',
            'exit_code':result.returncode,'source_stage':str(source),'runtime_log_sha256':sha(work/'runtime.log'),
            'qemu_sha256':sha(args.qemu),'seconds':round(time.monotonic()-started,2),
            'temporary_binfmt_registration':True,'registration_removed':True,'production_stage_modified':False,
            'PS2_kernel_or_devices_tested':False,'PS2_32_MiB_memory_tested':False,'physical_hardware_tested':False}
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
        if not passed:raise RuntimeError('Native SDK qualification failed; see '+str(work/'runtime.log'))
        print('Stripped native SDK qualification passed.',flush=True)
    finally:
        if registered:registration.write_text('-1')

if __name__=='__main__':main()
