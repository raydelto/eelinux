"""Command discovery and the expanded GNU/Linux administration build contract."""
from pathlib import Path
from source_archives import source_archive
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import tarfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import rootfs
import userland


class ToolContract(unittest.TestCase):
    def test_profile_disabled_choices_are_inherited_and_conflicts_fail(self):
        data={'profiles':{'base':{'symbols':[],'disabled_symbols':['BR2_PACKAGE_SQLITE_NO_SYNC']},
                          'desktop':{'symbols':[]},'development':{'symbols':[]}}}
        info=dict(major=16,headers=[5,4],path='/toolchain',prefix='mipsr5900el-linux-gnu',
                  features=dict.fromkeys(('openmp','ssp','ssp_strong','fortran','dlang','rpc'),False))
        self.assertIn('# BR2_PACKAGE_SQLITE_NO_SYNC is not set',userland.config_lines(data,info,'full'))
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'.config'
            path.write_text('BR2_mips_r5900=y\nBR2_MIPS_SOFT_FLOAT=y\nBR2_PACKAGE_SQLITE_NO_SYNC=y\n')
            with self.assertRaisesRegex(userland.BuildError,'enabled disabled profile options'):
                userland.check_resolved_config(path,data,'base')
        data['profiles']['base']['symbols'].append('BR2_PACKAGE_SQLITE_NO_SYNC')
        with self.assertRaisesRegex(userland.BuildError,'enables and disables the same'):
            userland.config_lines(data,info,'base')

    def test_origin_runtime_paths_accept_only_present_reachable_helpers(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            path = folder / 'usr/lib/gconv/module.so'
            path.parent.mkdir(parents=True)
            strings = b'\0libhelper.so\0$ORIGIN\0'
            dynamic_offset = 52 + 3 * 32
            attributes_offset = dynamic_offset + 5 * 8
            strings_offset = attributes_offset + 24
            size = strings_offset + len(strings)
            ident = b'\x7fELF\x01\x01\x01' + b'\0' * 9
            header = struct.pack('<16sHHIIIIIHHHHHH', ident, 3, 8, 1, 0, 52, 0,
                                 0x20921000, 52, 32, 3, 40, 0, 0)
            programs = (struct.pack('<8I', 1, 0, 0x1000, 0, size, size, 5, 4096)
                        + struct.pack('<8I', 2, dynamic_offset, 0, 0, 40, 40, 4, 4)
                        + struct.pack('<8I', 0x70000003, attributes_offset, 0, 0, 24, 24, 4, 8))
            dynamic = b''.join(struct.pack('<II', *entry) for entry in (
                (1, 1), (5, 0x1000 + strings_offset), (10, len(strings)),
                (15, strings.index(b'$')), (0, 0)))
            attributes = struct.pack('<H6B4I', 0, 3, 0, 1, 0, 0, 3, 6, 0, 0, 0)
            path.write_bytes(header + programs + dynamic + attributes + strings)
            info = rootfs.elf_info(path)
            self.assertEqual(info['search_paths'], ['$ORIGIN'])
            helper = path.parent / 'libhelper.so'
            helper.write_bytes(b'fixture')
            self.assertTrue(rootfs.has_runtime_library(folder, 'usr/lib/gconv/module.so', 'libhelper.so', info, set()))
            helper.unlink()
            (folder / 'libhelper.so').write_bytes(b'unreachable fixture')
            self.assertFalse(rootfs.has_runtime_library(folder, 'usr/lib/gconv/module.so', 'libhelper.so', info, set()))

    def test_catalog_covers_the_commands_checked_in_each_profile(self):
        for desktop in (False, True):
            with self.subTest(desktop=desktop):
                rows = [line.split('\t', 1) for line in rootfs.tool_catalog(desktop).splitlines()
                        if not line.startswith('#')]
                commands = {name for _, names in rows for name in names.split()}
                self.assertTrue(set(rootfs.GNU_COMMANDS + rootfs.SYSTEM_COMMANDS) - {'less', 'ps2-video'} <= commands)
                self.assertEqual('lsusb' in commands, desktop)
                self.assertTrue({'curl', 'ssh', 'rsync', 'strace', 'fsck.fat', 'jq', 'losetup', 'zramctl'} <= commands)
        self.assertNotIn('python3', rootfs.tool_catalog(development=False))
        self.assertIn('python3', rootfs.tool_catalog(development=True))

    def test_networking_and_https_are_selected_without_automatic_servers(self):
        info = dict(major=16, headers=[7, 2], path='/toolchain', prefix='mipsr5900el-linux-gnu',
                    features=dict.fromkeys(('openmp', 'ssp', 'ssp_strong', 'fortran', 'dlang', 'rpc'), False))
        lines = userland.config_lines(userland.manifest(), info, 'base')
        for symbol in ('OPENSSH_CLIENT', 'OPENSSH_KEY_UTILS', 'LIBCURL_CURL', 'LIBCURL_OPENSSL',
                       'CA_CERTIFICATES', 'UTIL_LINUX_LOSETUP', 'E2FSPROGS_DEBUGFS'):
            self.assertIn('BR2_PACKAGE_' + symbol + '=y', lines)
        self.assertIn('BR2_PACKAGE_OPENSSH_SERVER=y', lines)
        for symbol in ('BIND_SERVER', 'NTP_NTPD', 'NFS_UTILS_RPC_NFSD'):
            self.assertIn('# BR2_PACKAGE_' + symbol + ' is not set', lines)
        external = (ROOT / 'userland/external.mk').read_text()
        for package in ('OPENSSH', 'OPENVPN', 'ATFTP', 'TFTPD', 'IPTABLES', 'NTP',
                        'BIND', 'WPA_SUPPLICANT', 'RPCBIND', 'NFS_UTILS', 'RSYNC'):
            self.assertIn(package + '_INSTALL_INIT_SYSV =\n', external)
        self.assertIn('BR2_TOOLCHAIN_GLIBC_GCONV_LIBS_COPY=y', lines)



@unittest.skipUnless(sys.platform == 'linux', 'Bash runtime checks require Linux/WSL')
class ToolBrowser(unittest.TestCase):
    def test_shell_completion_is_loaded_only_for_interactive_bash_and_keeps_C(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            completion=folder/'completion.sh'
            completion.write_text('eelinux_completion_fixture=loaded\n')
            profile=folder/'profile'
            profile.write_text((ROOT/'rootfs/etc/profile').read_text().replace('/etc/profile.d/bash_completion.sh',str(completion)))
            for interactive in (False,True):
                command=('PS1=fixture; ' if interactive else 'unset PS1; ')+'. "$1"; printf "%s|%s" "${eelinux_completion_fixture:-absent}" "$LANG"'
                result=subprocess.run(['/bin/bash','--noprofile','--norc','-c',command,'bash',str(profile)],
                                      env=dict(os.environ,LANG='C'),capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(result.stdout,('loaded' if interactive else 'absent')+'|C')
            bashrc=folder/'bashrc'
            bashrc.write_text((ROOT/'rootfs/etc/skel/.bashrc').read_text().replace('/etc/profile.d/bash_completion.sh',str(completion)))
            for interactive in (False,True):
                result=subprocess.run(['/bin/bash','--noprofile','--norc','-ic' if interactive else '-c',
                    '. "$1"; printf "%s|%s" "${eelinux_completion_fixture:-absent}" "$LANG"','bash',str(bashrc)],
                    env=dict(os.environ,LANG='C'),capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(result.stdout,('loaded' if interactive else 'absent')+'|C')

    def run_browser(self, catalog, *args):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'tools.tsv'
            path.write_text(catalog)
            env = dict(os.environ, PS2_TOOLS_CATALOG=str(path))
            return subprocess.run(['/bin/bash', str(ROOT / 'rootfs/usr/bin/ps2-tools'), *args],
                                  env=env, capture_output=True, text=True)

    def test_lists_real_executable_paths_and_suite_filter(self):
        result = self.run_browser('shell\tbash\nmissing\teelinux-nonexistent-test-command\n', '--list')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r'bash\t/(usr/)?bin/bash\n')
        self.assertNotIn('nonexistent', result.stdout)
        result = self.run_browser('shell\tbash\nmissing\teelinux-nonexistent-test-command\n', 'shell')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('1 of 1', result.stdout)

    def test_missing_commands_fail_audit_and_unknown_suites_are_errors(self):
        result = self.run_browser('fixture\tbash eelinux-nonexistent-test-command\n', '--check')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Missing: eelinux-nonexistent-test-command (fixture)', result.stderr)
        result = self.run_browser('fixture\tbash\n', 'unknown')
        self.assertEqual(result.returncode, 2)

    def test_catalog_text_is_not_executed(self):
        result = self.run_browser('fixture\t$(false) `false` bash\n', '--check')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Missing: $(false)', result.stderr)
        self.assertIn('Missing: `false`', result.stderr)


@unittest.skipUnless(sys.platform == 'linux' and shutil.which('make') and shutil.which('cc'),
                     'The checksum hook and compiler regression require Linux build tools')
class LsfdCompatibility(unittest.TestCase):
    def test_resize2fs_mounted_metadata_hook_is_locked_and_keeps_offline_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            (folder/'resize').mkdir()
            source=folder/'resize/main.c'
            with tarfile.open(source_archive('e2fsprogs-1.47.4.tar.xz')) as archive:
                original=archive.extractfile('e2fsprogs-1.47.4/resize/main.c').read()
            source.write_bytes(original)
            makefile=folder/'Makefile'
            makefile.write_text(f'BR2_EXTERNAL_EELINUX_PATH := {ROOT/"userland"}\n'
                'E2FSPROGS_VERSION := 1.47.4\n'
                f'include {ROOT/"userland/external.mk"}\n'
                f'{folder/"patched"}:\n\t$(EELINUX_E2FSPROGS_1474_MOUNTED_READONLY_METADATA)\n')
            result=subprocess.run(['make','--no-print-directory','-f',str(makefile)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            corrected=source.read_bytes()
            # Every other operation is byte-identical: notably the offline
            # RW|EXCLUSIVE open, mounted read-only fs handle, and ioctl dispatch.
            self.assertEqual(corrected.replace(
                b'\t/* Online resizing writes through the mounted filesystem ioctl. */\n'
                b'\tif (print_min_size || (mount_flags & EXT2_MF_MOUNTED))',
                b'\tif (print_min_size)'),original)
            for text in (b'io_flags = EXT2_FLAG_RW | EXT2_FLAG_EXCLUSIVE;',
                         b'io_flags |= EXT2_FLAG_DIRECT_IO;',
                         b'retval = online_resize_fs(fs, mtpt, &new_size, flags);'):
                self.assertIn(text,corrected)
            # A later source change must fail before patching, rather than
            # accepting an unreviewed near-match with patch fuzz.
            drifted=original+b'\n/* unreviewed source change */\n'
            source.write_bytes(drifted)
            result=subprocess.run(['make','--no-print-directory','-f',str(makefile)],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(source.read_bytes(),drifted)
            for version,expected in (('1.47.4',True),('1.47.3',False),('1.47.5',False)):
                makefile.write_text(f'BR2_EXTERNAL_EELINUX_PATH := {ROOT/"userland"}\n'
                    f'E2FSPROGS_VERSION := {version}\n'
                    f'include {ROOT/"userland/external.mk"}\n'
                    'all:\n\t@printf "%s\\n" "$(E2FSPROGS_POST_PATCH_HOOKS)"\n')
                result=subprocess.run(['make','--no-print-directory','-f',str(makefile)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual('EELINUX_E2FSPROGS_1474_MOUNTED_READONLY_METADATA' in result.stdout,expected)

    def test_current_htop_patch_is_source_locked_and_restores_internal_bool(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);source=folder/'ProvideCurses.h'
            with tarfile.open(source_archive('htop-3.5.3.tar.xz')) as stream:
                source.write_bytes(stream.extractfile('htop-3.5.3/ProvideCurses.h').read())
            makefile=folder/'Makefile'
            makefile.write_text(f'BR2_EXTERNAL_EELINUX_PATH := {ROOT/"userland"}\n'
                f'include {ROOT/"userland/external.mk"}\n'
                f'{folder/"patched"}:\n\t$(EELINUX_HTOP_CURRENT_CURSES_BOOL_SCOPE)\n')
            result=subprocess.run(['make','--no-print-directory','-f',str(makefile)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            (folder/'config.h').write_text('#define HAVE_CURSES_H 1\n')
            (folder/'curses.h').write_text('#define NCURSES_BOOL int\n#undef bool\n#define bool NCURSES_BOOL\nextern bool curses_result(void);\n')
            (folder/'app.h').write_text('#include <stdbool.h>\nbool app_result(void);\n')
            (folder/'provider.c').write_text('#include "app.h"\n#include "ProvideCurses.h"\n'
                '_Static_assert(__builtin_types_compatible_p(__typeof__(curses_result), int(void)), "curses ABI kept");\n'
                'bool app_result(void) { return true; }\n')
            (folder/'consumer.c').write_text('#include "ProvideCurses.h"\n#include "app.h"\n'
                '_Static_assert(__builtin_types_compatible_p(__typeof__(app_result), _Bool(void)), "app ABI kept");\n'
                'int main(void) { return app_result()!=true; }\n')
            binary=folder/'probe'
            result=subprocess.run(['cc','-std=gnu17','-Werror','-DHAVE_CONFIG_H','-I'+str(folder),str(folder/'provider.c'),
                str(folder/'consumer.c'),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(subprocess.run([str(binary)]).returncode,0)

    def test_htop_curses_wrapper_preserves_public_and_internal_boolean_types(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);source=folder/'ProvideCurses.h'
            with tarfile.open(source_archive('htop-3.3.0.tar.xz')) as stream:
                source.write_bytes(stream.extractfile('htop-3.3.0/ProvideCurses.h').read())
            makefile=folder/'Makefile'
            makefile.write_text(f'BR2_EXTERNAL_EELINUX_PATH := {ROOT/"userland"}\n'
                f'include {ROOT/"userland/external.mk"}\n'
                f'{folder/"patched"}:\n\t$(EELINUX_HTOP_CURSES_BOOL_SCOPE)\n')
            result=subprocess.run(['make','--no-print-directory','-f',str(makefile)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            (folder/'config.h').write_text('#define HAVE_CURSES_H 1\n')
            (folder/'curses.h').write_text('#define NCURSES_BOOL unsigned char\n#undef bool\n#define bool NCURSES_BOOL\nextern bool curses_result(void);\n')
            (folder/'app.h').write_text('#include <stdbool.h>\nbool app_result(void);\n')
            (folder/'provider.c').write_text('#include "app.h"\n#include "ProvideCurses.h"\n'
                '_Static_assert(__builtin_types_compatible_p(__typeof__(curses_result), unsigned char(void)), "curses ABI kept");\n'
                'bool app_result(void) { return true; }\n')
            (folder/'consumer.c').write_text('#include "ProvideCurses.h"\n#include "app.h"\n'
                '_Static_assert(__builtin_types_compatible_p(__typeof__(app_result), _Bool(void)), "app ABI kept");\n'
                'int main(void) { return app_result()!=true; }\n')
            binary=folder/'probe'
            result=subprocess.run(['cc','-std=gnu17','-Werror','-I'+str(folder),str(folder/'provider.c'),
                str(folder/'consumer.c'),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(subprocess.run([str(binary)]).returncode,0)

    def test_expect_tty_patch_supplies_the_existing_function_prototype(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);source=folder/'exp_tty.h'
            with tarfile.open(source_archive('expect5.45.4.tar.gz')) as stream:
                source.write_bytes(stream.extractfile('expect5.45.4/exp_tty.h').read())
            makefile=folder/'Makefile'
            makefile.write_text(f'BR2_EXTERNAL_EELINUX_PATH := {ROOT/"userland"}\n'
                f'include {ROOT/"userland/external.mk"}\n'
                f'{folder/"patched"}:\n\t$(EELINUX_EXPECT_TTY_PROTOTYPE)\n')
            result=subprocess.run(['make','--no-print-directory','-f',str(makefile)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            (folder/'expect_cf.h').write_text('/* configured header fixture */\n')
            probe=folder/'tty.c'
            probe.write_text('typedef struct Tcl_Interp Tcl_Interp;\ntypedef struct exp_tty exp_tty;\n'
                '#include "exp_tty.h"\n'
                'int check(Tcl_Interp *interp, exp_tty *tty) { int raw,echo; return exp_tty_cooked_echo(interp,tty,&raw,&echo); }\n')
            result=subprocess.run(['cc','-std=gnu17','-Werror','-fsyntax-only','-I'+str(folder),str(probe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_htop_filter_patch_passes_boolean_with_integer_curses_abi(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);source=folder/'CommandLine.c'
            with tarfile.open(source_archive('htop-3.3.0.tar.xz')) as stream:
                source.write_bytes(stream.extractfile('htop-3.3.0/CommandLine.c').read())
            makefile=folder/'Makefile'
            makefile.write_text(f'BR2_EXTERNAL_EELINUX_PATH := {ROOT/"userland"}\n'
                f'include {ROOT/"userland/external.mk"}\n'
                f'{folder/"patched"}:\n\t$(EELINUX_HTOP_EXPLICIT_FILTER_BOOL)\n')
            result=subprocess.run(['make','--no-print-directory','-f',str(makefile)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            call=re.search(r'MainPanel_updateLabels\(panel,[^;]+;',source.read_text()).group()
            probe=folder/'truth.c'
            probe.write_text('#include <stddef.h>\ntypedef unsigned char bool;\n'
                'void MainPanel_updateLabels(void *, bool, bool);\n'
                'void check(void) { void *panel=0; struct { char *commFilter; } flags={0};\n'
                'struct { bool treeView; } ss={0}; struct { void *unused; __typeof__(ss) *ss; } value={0,&ss}, *settings=&value;\n'
                +call+'\n}\n')
            result=subprocess.run(['cc','-std=gnu17','-Werror','-fsyntax-only',str(probe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_iptraf_bool_patch_keeps_curses_abi_and_standard_constants(self):
        archive=source_archive('iptraf-ng-1.2.2.tar.gz')
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);source=folder/'src/iptraf-ng-compat.h';source.parent.mkdir()
            with tarfile.open(archive) as stream:
                source.write_bytes(stream.extractfile('iptraf-ng-1.2.2/src/iptraf-ng-compat.h').read())
            makefile=folder/'Makefile'
            makefile.write_text(f'BR2_EXTERNAL_EELINUX_PATH := {ROOT/"userland"}\n'
                f'include {ROOT/"userland/external.mk"}\n'
                f'{folder/"patched"}:\n\t$(EELINUX_IPTRAF_STANDARD_BOOL)\n')
            result=subprocess.run(['make','--no-print-directory','-f',str(makefile)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            headers=folder/'headers';headers.mkdir()
            (headers/'curses.h').write_text('#define NCURSES_BOOL unsigned char\n#undef bool\n#define bool NCURSES_BOOL\n')
            (headers/'panel.h').write_text('/* ncurses panel fixture */\n')
            probe=folder/'bool.c'
            probe.write_text('#include "iptraf-ng-compat.h"\n'
                '_Static_assert(sizeof(bool)==sizeof(NCURSES_BOOL), "ncurses ABI unchanged");\n'
                'int main(void) { bool flag=true; return flag==false; }\n')
            result=subprocess.run(['cc','-D_GNU_SOURCE','-std=gnu99','-Werror','-fsyntax-only',
                '-I'+str(headers),'-I'+str(source.parent),str(probe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_real_lsfd_patch_handles_function_like_bsearch_macro(self):
        archive = source_archive('util-linux-2.40.4.tar.xz')
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source = folder / 'misc-utils/lsfd.c'
            source.parent.mkdir()
            with tarfile.open(archive) as stream:
                source.write_bytes(stream.extractfile('util-linux-2.40.4/misc-utils/lsfd.c').read())
            makefile = folder / 'Makefile'
            makefile.write_text(
                f'BR2_EXTERNAL_EELINUX_PATH := {ROOT / "userland"}\n'
                f'include {ROOT / "userland/external.mk"}\n'
                f'{folder / "patched"}:\n\t$(EELINUX_UTIL_LINUX_LSFD_BSEARCH)\n')
            result = subprocess.run(['make', '--no-print-directory', '-f', str(makefile)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            body = re.search(r'int fd = file->association;.*?file->multiplexed = 1;',
                             source.read_text(), re.S).group()
            probe = folder / 'macro.c'
            probe.write_text('#include <poll.h>\n'
                             '#define bsearch(KEY, BASE, COUNT, SIZE, COMPARE) ((void *)0)\n'
                             'int main(void) {\n'
                             'struct { int association, multiplexed; } value = {0}, *file = &value;\n'
                             + body + '\nreturn 0;\n}\n')
            result = subprocess.run(['cc', '-std=gnu17', '-Werror', '-fsyntax-only', str(probe)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_real_strace_patch_preserves_const_search_result(self):
        archive = source_archive('strace-6.13.tar.xz')
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source = folder / 'src/ioctl.c'
            source.parent.mkdir()
            with tarfile.open(archive) as stream:
                source.write_bytes(stream.extractfile('strace-6.13/src/ioctl.c').read())
            makefile = folder / 'Makefile'
            makefile.write_text(
                f'BR2_EXTERNAL_EELINUX_PATH := {ROOT / "userland"}\n'
                f'include {ROOT / "userland/external.mk"}\n'
                f'{folder / "patched"}:\n\t$(EELINUX_STRACE_CONST_BSEARCH)\n')
            result = subprocess.run(['make', '--no-print-directory', '-f', str(makefile)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            lookup = re.search(r'static const struct_ioctlent \*\nioctl_lookup.*?\n}',
                               source.read_text(), re.S).group()
            probe = folder / 'const.c'
            probe.write_text('#include <stddef.h>\n#include <stdint.h>\n'
                             'typedef struct { unsigned int code; } struct_ioctlent;\n'
                             'static const struct_ioctlent ioctlent[1] = {{1}};\n'
                             'static const unsigned int nioctlents = 1;\n'
                             '#define bsearch(KEY, BASE, COUNT, SIZE, COMPARE) (BASE)\n'
                             + lookup + '\nint main(void) { return ioctl_lookup(1)->code != 1; }\n')
            result = subprocess.run(['cc', '-std=gnu17', '-Werror', '-fsyntax-only', str(probe)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_unzip_hooks_apply_to_the_pinned_cmake_recipe_and_install_zipinfo(self):
        baseline = (ROOT / 'userland/package/unzip/0101-Add-a-CMakeFile.txt-to-ease-cross-compilation.patch').read_text()
        cmake = '\n'.join(line[1:] for line in baseline.splitlines()
                          if line.startswith('+') and not line.startswith('+++')) + '\n'
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'CMakeLists.txt').write_text(cmake)
            binary = folder / 'stage/usr/bin/unzip'
            binary.parent.mkdir(parents=True)
            binary.write_text('test fixture')
            makefile = folder / 'Makefile'
            makefile.write_text(
                f'BR2_EXTERNAL_EELINUX_PATH := {ROOT / "userland"}\n'
                f'TARGET_DIR := {folder / "stage"}\n'
                f'include {ROOT / "userland/external.mk"}\n'
                f'{folder / "patched"}:\n\t$(EELINUX_UNZIP_FULL_TOOLS)\n'
                '\t$(EELINUX_UNZIP_ZIPINFO_ALIAS)\n')
            result = subprocess.run(['make', '--no-print-directory', '-f', str(makefile)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((binary.parent / 'zipinfo').readlink(), Path('unzip'))
            self.assertIn('install(TARGETS funzip unzipsfx DESTINATION bin)',
                          (folder / 'CMakeLists.txt').read_text())


if __name__ == '__main__':
    unittest.main()
