"""Stopping a hung session must preserve unrelated and reused processes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import emulator_process as processes


class SessionProcess(unittest.TestCase):
    def test_timeout_kills_owned_child_and_marks_it_inactive(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with self.assertRaises(subprocess.TimeoutExpired):
                processes.run_process([sys.executable, '-c', 'import time; time.sleep(30)'],
                                      cwd=directory, env=os.environ.copy(), timeout=0.2)
            record = json.loads((directory / 'running-process.json').read_text())
            self.assertFalse(record['active'])
            self.assertFalse(processes.stop_process(directory))

    def test_stop_uses_exact_identity_and_rejects_stale_pid(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
            try:
                identity = processes.process_identity(child.pid)
                self.assertIsNotNone(identity)
                record = directory / 'running-process.json'
                stale = {**identity, 'creation': identity['creation'] - 1}
                record.write_text(json.dumps({'pid': child.pid, 'identity': stale, 'active': True}))
                with self.assertRaisesRegex(ValueError, 'PID was reused'):
                    processes.stop_process(directory)
                self.assertIsNone(child.poll())
                record.write_text(json.dumps({'pid': child.pid, 'identity': identity, 'active': True}))
                self.assertTrue(processes.stop_process(directory))
                child.wait(timeout=5)
                self.assertIsNotNone(child.returncode)
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()

    def test_failed_identity_capture_does_not_orphan_the_launched_process(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(processes, 'process_identity', side_effect=OSError('cannot query')):
                with self.assertRaisesRegex(OSError, 'cannot query'):
                    processes.run_process([sys.executable, '-c', 'import time; time.sleep(30)'],
                                          cwd=Path(temporary), env=os.environ.copy(), timeout=5)
            record = json.loads((Path(temporary) / 'running-process.json').read_text())
            self.assertFalse(record['active'])


if __name__ == '__main__':
    unittest.main()
