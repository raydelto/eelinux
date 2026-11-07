"""Check the real Expect hooks and its initializer against pinned Tcl headers."""
from pathlib import Path
from source_archives import source_archive
import re, shutil, subprocess, sys, tarfile, tempfile, unittest

ROOT=Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform=='linux' and shutil.which('make') and shutil.which('cc'),
                     'Linux build tools required')
class ExpectChannelApi(unittest.TestCase):
    def test_real_channel_callbacks_resolve_through_tcls_versioned_interface(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            names=['exp_int.h','pty_termios.c','exp_chan.c','exp_clib.c','exp_tty.h']
            with tarfile.open(source_archive('expect5.45.4.tar.gz')) as archive:
                for name in names:
                    (folder/name).write_bytes(archive.extractfile('expect5.45.4/'+name).read())
            makefile=folder/'Makefile'
            makefile.write_text(f'BR2_EXTERNAL_EELINUX_PATH := {ROOT/"userland"}\n'
                                f'include {ROOT/"userland/external.mk"}\n'
                                f'{folder/"patched"}:\n'
                                '\t$(EELINUX_EXPECT_TTY_PROTOTYPE)\n'
                                '\t$(EELINUX_EXPECT_POSIX_PROTOTYPES)\n'
                                '\t$(EELINUX_EXPECT_CHANNEL_API)\n')
            result=subprocess.run(['make','--no-print-directory','-f',str(makefile)],
                                  capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            with tarfile.open(source_archive('tcl8.6.15-src.tar.gz')) as archive:
                for name in ('tcl.h','tclDecls.h','tclPlatDecls.h'):
                    (folder/name).write_bytes(archive.extractfile('tcl8.6.15/generic/'+name).read())
                channel_source=archive.extractfile('tcl8.6.15/generic/tclIO.c').read().decode()
            # Compile the actual Tcl dispatch functions: a syntactically valid
            # VERSION_1 initializer instead makes Tcl call the address 0x1.
            dispatch='\n'.join(re.search(
                r'\n'+return_type+r'\n'+name+r'\(.*?\n}',channel_source,re.S).group()
                for return_type,name in (
                    (r'Tcl_ChannelTypeVersion','Tcl_ChannelVersion'),
                    (r'Tcl_DriverBlockModeProc \*','Tcl_ChannelBlockModeProc')))
            initializer=re.search(r'Tcl_ChannelType expChannelType = \{.*?\n};',
                                  (folder/'exp_chan.c').read_text(),re.S).group()
            probe=folder/'channel.c'
            probe.write_text('#define USE_NON_CONST 1\n#include "tcl.h"\n'
                'extern int ExpCloseProc(ClientData,Tcl_Interp *);\n'
                'extern int ExpInputProc(ClientData,char *,int,int *);\n'
                'extern int ExpOutputProc(ClientData,char *,int,int *);\n'
                'extern void ExpWatchProc(ClientData,int);\n'
                'extern int ExpGetHandleProc(ClientData,int,ClientData *);\n'
                'extern int ExpBlockModeProc(ClientData,int);\n'+initializer+'\n'+dispatch+'\n'
                'int ExpCloseProc(ClientData d,Tcl_Interp *i){(void)d;(void)i;return 0;}\n'
                'int ExpInputProc(ClientData d,char *b,int n,int *e){(void)d;(void)b;(void)n;(void)e;return 0;}\n'
                'int ExpOutputProc(ClientData d,char *b,int n,int *e){(void)d;(void)b;(void)n;(void)e;return 0;}\n'
                'void ExpWatchProc(ClientData d,int m){(void)d;(void)m;}\n'
                'int ExpGetHandleProc(ClientData d,int m,ClientData *h){(void)d;(void)m;(void)h;return 0;}\n'
                'int ExpBlockModeProc(ClientData d,int m){(void)d;return m==TCL_MODE_NONBLOCKING?37:0;}\n'
                'int main(void){Tcl_DriverBlockModeProc *p=Tcl_ChannelBlockModeProc(&expChannelType);'
                'if(Tcl_ChannelVersion(&expChannelType)!=TCL_CHANNEL_VERSION_2||p!=ExpBlockModeProc)return 1;'
                'return p(0,TCL_MODE_NONBLOCKING)==37?0:2;}\n')
            result=subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-fsyntax-only',str(probe)],
                                  capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror',str(probe),'-o',str(folder/'channel')],
                                  capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(folder/'channel')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)


if __name__=='__main__':unittest.main()
