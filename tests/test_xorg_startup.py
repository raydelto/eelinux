"""Session ordering, resource overrides, failure paths and child cleanup."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == 'posix' and shutil.which('bash'), 'requires Bash process control')
class XorgStartup(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary.name)
        self.tools = self.folder / 'bin'
        self.tools.mkdir()
        self.home = self.folder / 'home'
        self.home.mkdir()
        self.log = self.folder / 'events'
        self.stock = self.folder / 'stock.twmrc'
        self.stock.write_text('# new stock\n')
        self.resources = self.folder / 'system.Xresources'
        self.resources.write_text('XTerm*background: black\n')
        self.config_copy = self.folder / 'active.twmrc'
        self.stub = self.tools / 'tool'
        self.stub.write_text('''#!/bin/bash
name=${0##*/}
printf '%s:%s\\n' "$name" "$*" >> "$EVENT_LOG"
case $name in
  ps2-xdg-menu)
    [[ ${MENU_FAIL:-0} == 0 ]] || exit 4
    printf 'menu "applications" { "Applications" f.title }\\n'
    ;;
  twm)
    cat "$2" > "$CONFIG_COPY"
    [[ ${TWM_FAIL:-0} == 0 ]] || exit 7
    while [[ ! -e $STOP_MANAGER ]]; do sleep 0.02; done
    ;;
  ps2-desktop)
    while [[ ! -e $RELEASE_WALLPAPER ]]; do sleep 0.02; done
    ;;
  xclock|xterm)
    trap 'exit 0' TERM
    while :; do sleep 0.02; done
    ;;
esac
''')
        self.stub.chmod(0o755)
        for name in ('xrdb', 'xset', 'ps2-desktop', 'ps2-xdg-menu', 'twm', 'xclock', 'xterm'):
            (self.tools / name).symlink_to(self.stub)
        script = (ROOT / 'rootfs/etc/X11/xinit/xinitrc').read_text()
        script = script.replace('export PATH=/usr/sbin:/usr/bin:/sbin:/bin',
                                'export PATH="' + str(self.tools) + ':/usr/bin:/bin"')
        script = script.replace('/etc/skel/.twmrc', str(self.stock))
        script = script.replace('/etc/X11/ps2.Xresources', str(self.resources))
        script = script.replace('/usr/bin/ps2-xdg-menu', str(self.tools / 'ps2-xdg-menu'))
        script = script.replace('/usr/bin/ps2-desktop', str(self.tools / 'ps2-desktop'))
        self.script = self.folder / 'xinitrc'
        self.script.write_text(script)
        self.environment = dict(os.environ, HOME=str(self.home), TMPDIR=str(self.folder),
                                XDG_DATA_DIRS='/custom', XDG_CACHE_HOME=str(self.folder / 'cache'),
                                EVENT_LOG=str(self.log), CONFIG_COPY=str(self.config_copy),
                                STOP_MANAGER=str(self.folder / 'stop'),
                                RELEASE_WALLPAPER=str(self.folder / 'wallpaper'))
        self.process = None

    def tearDown(self):
        (self.folder / 'stop').touch()
        (self.folder / 'wallpaper').touch()
        if self.process and self.process.poll() is None:
            self.process.terminate()
            self.process.communicate(timeout=5)
        self.temporary.cleanup()

    def start(self, **updates):
        self.process = subprocess.Popen(['bash', str(self.script)], env=self.environment | updates,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return self.process

    def events(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def wait_for(self, prefix):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if any(event.startswith(prefix) for event in self.events()):
                return
            if self.process.poll() is not None:
                break
            time.sleep(0.02)
        self.fail('Missing event: ' + prefix + '; events=' + repr(self.events()))

    def finish(self, code=0):
        (self.folder / 'stop').touch()
        output, error = self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, code, output + error)
        self.assertEqual(list(self.folder.glob('ps2-session.*')), [])

    def test_manager_starts_without_waiting_for_wallpaper_and_empty_personal_resources(self):
        (self.home / '.Xresources').write_text('! personal settings go here\n\n')
        self.start()
        self.wait_for('twm:')
        self.wait_for('ps2-desktop:')
        self.wait_for('xterm:')
        self.assertFalse((self.folder / 'wallpaper').exists())
        self.wait_for('xset:')
        self.assertEqual([event for event in self.events() if event.startswith('xset:')], ['xset:s off -dpms'])
        self.assertEqual(len([event for event in self.events() if event.startswith('xrdb:')]), 1)
        self.finish()

    def test_custom_template_and_cpp_resources_are_preserved(self):
        (self.home / '.twmrc').write_text('# custom configuration\n')
        (self.home / '.Xresources').write_text('#define SHADE black\nXTerm*background: SHADE\n')
        self.start()
        self.wait_for('twm:')
        self.wait_for('xterm:')
        self.assertTrue(self.config_copy.read_text().startswith('# custom configuration\n'))
        calls = [event for event in self.events() if event.startswith('xrdb:')]
        self.assertEqual(len(calls), 2)
        self.assertNotIn('-nocpp', calls[1])
        self.finish()

    def test_manager_failure_cleans_session_and_returns_status(self):
        self.start(TWM_FAIL='1')
        self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 7)
        self.assertEqual(list(self.folder.glob('ps2-session.*')), [])

    def test_menu_failure_never_starts_manager(self):
        self.start(MENU_FAIL='1')
        self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 1)
        self.assertFalse(any(event.startswith('twm:') for event in self.events()))
        self.assertEqual(list(self.folder.glob('ps2-session.*')), [])

    def assert_stock_migrates(self, crlf):
        original = (ROOT / 'tests/fixtures/twmrc-r43').read_bytes()
        if crlf:
            original = original.replace(b'\n', b'\r\n')
        personal = self.home / '.twmrc'
        personal.write_bytes(original)
        self.start()
        self.wait_for('xterm:')
        self.assertTrue(self.config_copy.read_text().startswith('# new stock\n'))
        self.assertEqual(personal.read_bytes(), original)
        self.finish()

    def test_exact_prior_stock_controls_use_new_template(self):
        self.assert_stock_migrates(False)

    def test_exact_prior_stock_controls_with_crlf_use_new_template(self):
        self.assert_stock_migrates(True)

    def test_termination_cleans_background_clients(self):
        self.start()
        self.wait_for('xterm:')
        self.process.terminate()
        self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 143)
        self.assertEqual(list(self.folder.glob('ps2-session.*')), [])


if __name__ == '__main__':
    unittest.main()
