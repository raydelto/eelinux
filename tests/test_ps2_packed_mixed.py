"""Mixed original-precision colour and coverage in the GS ten-bit compositor."""
import subprocess,unittest
from pathlib import Path
from unittest.mock import patch
import test_ps2_composite as _composite
ROOT=_composite.ROOT
ROP_SOURCES=_composite.ROP_SOURCES
CASES=('mixed-guards',)+tuple(f'mixed-{role}-{filter}' for role in range(6) for filter in range(3))

class PackedMixed(_composite.NativeComposite):
    @classmethod
    def setUpClass(cls):
        inherited=subprocess.run
        def compile_fixture(argv,**kwargs):
            if str(ROOT/'tests/graphics/ps2-composite.c') in argv:
                (Path(cls.folder.name)/'previous-composite.c').write_text(
                    (ROOT/'tests/graphics/ps2-composite.c').read_text().replace('int main(','int composite_fixture_main(',1))
            argv=[str(ROOT/'tests/graphics/ps2-packed-mixed.c') if v==str(ROOT/'tests/graphics/ps2-composite.c') else v for v in argv]
            return inherited(argv,**kwargs)
        with patch('subprocess.run',compile_fixture):super().setUpClass()
    def run_case(self,name):subprocess.run([str(self.binary),name],check=True,timeout=1800)

for _name in tuple(dir(_composite.NativeComposite)):
    if _name.startswith('test_'):setattr(PackedMixed,_name,None)
for _case in CASES:
    setattr(PackedMixed,'test_'+_case.replace('-','_'),lambda self,n=_case:self.run_case(n))
if __name__=='__main__':unittest.main()
