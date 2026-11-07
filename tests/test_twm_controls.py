"""Build the shipped TWM patch set and exercise real decorations on Xvfb.

Run on a Linux host with Xvfb, pkg-config, make, a C compiler and the X11,
Xext, Xt, Xmu, ICE, SM and Xtst development packages. No target hardware or
prebuilt eeLinux image is required.
"""
from pathlib import Path
from source_archives import source_archive
import os
import shutil
import shlex
import struct
import subprocess
import tarfile
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == 'posix' and all(shutil.which(x) for x in
                     ('cc', 'make', 'patch', 'pkg-config', 'Xvfb')),
                     'Linux host with Xvfb and X11 build tools required')
class TwmControls(unittest.TestCase):
    def test_window_operations(self):
        packages = ['x11', 'xext', 'xt', 'xmu', 'ice', 'sm', 'xtst']
        if subprocess.run(['pkg-config', '--exists', *packages]).returncode:
            self.skipTest('X11/Xt/Xmu/Xtst development packages required')
        with tempfile.TemporaryDirectory(prefix='eelinux-twm-') as folder:
            work = Path(folder)
            with tarfile.open(source_archive('twm-1.0.13.tar.xz')) as archive:
                archive.extractall(work, filter='data')
            source = work / 'twm-1.0.13'
            with (work / 'build.log').open('w+') as log:
                for patch in sorted((ROOT / 'userland/package/x11r7/xapp_twm').glob('*.patch')):
                    subprocess.run(['patch', '--batch', '-p1', '-i', str(patch)],
                                   cwd=source, check=True, stdout=log, stderr=log)
                for command in ([str(source / 'configure')], ['make', '-j2']):
                    result = subprocess.run(command, cwd=source, stdout=log, stderr=log)
                    if result.returncode:
                        log.seek(0)
                        self.fail(log.read()[-12000:])
            flags = subprocess.check_output(['pkg-config', '--cflags', '--libs',
                                            'x11', 'xext', 'xtst'], text=True).split()
            probe = work / 'controls'
            subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/windows/twm-controls.c'), '-o', str(probe),
                            *flags], check=True)
            config = (ROOT / 'rootfs/etc/skel/.twmrc').read_text()
            config = config.replace('/usr/share/ps2/twm', str(ROOT / 'rootfs/usr/share/ps2/twm'))
            # The generated application submenu is provided by xinitrc at runtime.
            config += '\nmenu "applications" { "Menu fixture" f.title "Hide list" f.hideiconmgr }\n'
            (work / 'twmrc').write_text(config)
            # -displayfd selects an unused display, allowing concurrent test runs.
            # WSLg mounts /tmp/.X11-unix read-only. Use an authenticated TCP
            # connection so this test also runs there without changing that mount.
            auth = work / 'Xauthority'
            fields = [b'', b'', b'MIT-MAGIC-COOKIE-1', os.urandom(16)]
            auth.write_bytes(struct.pack('!H', 65535) + b''.join(
                struct.pack('!H', len(field)) + field for field in fields))
            auth.chmod(0o600)
            read_fd, write_fd = os.pipe()
            xvfb_log = (work / 'xvfb.log').open('w+')
            xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen',
                                     '0', '800x600x24', '-nolisten', 'unix',
                                     '-listen', 'tcp', '-auth', str(auth)],
                                    pass_fds=(write_fd,), stdout=subprocess.DEVNULL,
                                    stderr=xvfb_log)
            os.close(write_fd)
            wm = None
            try:
                import select
                self.assertTrue(select.select([read_fd], [], [], 10)[0], 'Xvfb startup timeout')
                display_number = os.read(read_fd, 32).decode().strip()
                if not display_number:
                    xvfb_log.seek(0)
                    self.fail('Xvfb did not select a display:\n' + xvfb_log.read(4000))
                display = 'localhost:' + display_number
                env = dict(os.environ, DISPLAY=display, HOME=folder, LC_ALL='C',
                           XAUTHORITY=str(auth))
                with (work / 'twm.log').open('w+') as wm_log:
                    # Also exercise an ABI-checked target binary under the
                    # project's R5900 Linux-user emulator when requested.
                    wm_command = shlex.split(os.environ.get('EELINUX_TWM_TEST_RUNNER', ''))
                    wm_command += [os.environ.get('EELINUX_TWM_TEST_BINARY', str(source / 'src/twm')),
                                   '-f', str(work / 'twmrc')]
                    wm = subprocess.Popen(wm_command,
                                          env=env, stdout=wm_log, stderr=wm_log)
                    time.sleep(0.3)
                    if wm.poll() is not None:
                        wm_log.seek(0)
                        self.fail('TWM exited during startup:\n' + wm_log.read())
                    result = subprocess.run([str(probe)], env=env, text=True,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                            timeout=40)
                    wm_log.flush()
                    wm_log.seek(0)
                    self.assertEqual(result.returncode, 0, result.stdout + '\n' + wm_log.read())
                    self.assertIn('TWM controls: all checks passed', result.stdout)
                    self.assertIsNone(wm.poll(), 'TWM crashed during window operations')
            finally:
                os.close(read_fd)
                for process in (wm, xvfb):
                    if process is not None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                xvfb_log.close()


if __name__ == '__main__':
    unittest.main()
