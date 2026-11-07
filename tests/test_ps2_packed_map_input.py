"""Immutable GS inputs sharing either writable ten-bit backing."""
import subprocess, unittest
from pathlib import Path
from unittest.mock import patch
import test_ps2_composite as _composite
ROOT=_composite.ROOT
ROP_SOURCES=_composite.ROP_SOURCES

CASES=('write-input-alias',)

class PackedMapInput(_composite.NativeComposite):
    @classmethod
    def setUpClass(cls):
        inherited=subprocess.run
        def compile_fixture(argv, **kwargs):
            if str(ROOT/'tests/graphics/ps2-composite.c') in argv:
                (Path(cls.folder.name)/'previous-composite.c').write_text(
                    (ROOT/'tests/graphics/ps2-composite.c').read_text().replace('int main(', 'int composite_fixture_main(',1))
            argv=[str(ROOT/'tests/graphics/ps2-packed-map-input.c') if v==str(ROOT/'tests/graphics/ps2-composite.c') else v for v in argv]
            return inherited(argv,**kwargs)
        with patch('subprocess.run',compile_fixture):super().setUpClass()
    def test_write_input_alias(self):self.run_case(CASES[0])
    def run_case(self,name):subprocess.run([str(self.binary),name],check=True,timeout=1800)

# Inherit setup and the packet model, not the parent's full case matrix.
for _name in tuple(dir(_composite.NativeComposite)):
    if _name.startswith('test_') and _name!='test_write_input_alias':setattr(PackedMapInput,_name,None)

if __name__=='__main__':unittest.main()
