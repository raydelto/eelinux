"""Window/backing coordinate state through the actual native EXA hooks."""
import subprocess,unittest
from pathlib import Path
from unittest.mock import patch
import test_ps2_composite as _composite
ROOT=_composite.ROOT
CASES=('affine','projective','redirected','negative','extreme','repeat','overflow')

class WindowSampling(_composite.NativeComposite):
    @classmethod
    def setUpClass(cls):
        inherited=subprocess.run
        def compile_fixture(argv,**kwargs):
            if str(ROOT/'tests/graphics/ps2-composite.c') in argv:
                (Path(cls.folder.name)/'previous-composite.c').write_text(
                    (ROOT/'tests/graphics/ps2-composite.c').read_text().replace('int main(','int composite_fixture_main(',1))
            argv=[str(ROOT/'tests/graphics/ps2-window-sampling.c') if v==str(ROOT/'tests/graphics/ps2-composite.c') else v for v in argv]
            if argv[0]=='cc' or argv[0].endswith('gcc'):argv=[*argv,'-DCOMPOSITE']
            return inherited(argv,**kwargs)
        with patch('subprocess.run',compile_fixture):super().setUpClass()
for _name in tuple(dir(_composite.NativeComposite)):
    if _name.startswith('test_'):setattr(WindowSampling,_name,None)
for _case in CASES:
    setattr(WindowSampling,'test_'+_case,lambda self,n=_case:self.run_case(n))
if __name__=='__main__':unittest.main()
