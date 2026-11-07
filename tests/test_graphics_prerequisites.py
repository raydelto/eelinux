"""Missing optional SDK inputs skip integration; compiler failures still fail."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import test_ps2_surface_backends as backends
import test_ps2_opaque_probe as opaque_probe


class GraphicsPrerequisites(unittest.TestCase):
    def test_only_missing_required_headers_skip_the_integration_flags(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with patch.object(backends, 'SYSROOT', root/'sdk'), \
                 patch.object(backends, 'MESA', root/'mesa'), \
                 patch.object(backends, 'GENERATED', root/'generated'):
                for relative in ('sdk/usr/include/xorg/exa.h',
                                 'mesa/src/gallium/include/pipe/p_state.h',
                                 'generated/nir_opcodes.h'):
                    path = root/relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text('/* prerequisite fixture */\n')
                with self.assertRaisesRegex(unittest.SkipTest, 'builtin_types.h'):
                    backends.backend_flags()
                (root/'generated/builtin_types.h').write_text('/* prerequisite fixture */\n')
                self.assertIn('-isystem'+str(root/'generated'), backends.backend_flags())

    def test_opaque_probe_does_not_convert_a_compiler_error_into_a_skip(self):
        with patch('test_ps2_opaque_probe.copy_flags', return_value=[]), \
             patch('test_ps2_opaque_probe.subprocess.run',
                   side_effect=subprocess.CalledProcessError(1, ['cc'])):
            try:
                with self.assertRaises(subprocess.CalledProcessError):
                    opaque_probe.OpaqueProbe.setUpClass()
            finally:
                if hasattr(opaque_probe.OpaqueProbe, 'folder'):
                    opaque_probe.OpaqueProbe.folder.cleanup()


if __name__ == '__main__':
    unittest.main()
