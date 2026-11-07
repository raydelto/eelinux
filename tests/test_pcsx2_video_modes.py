"""Compile PCSX2's actual mode selector and output tables, with a register scaffold."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('PS2_PCSX2_SOURCE', str(ROOT/'build/emulator/pcsx2-windows-source')))


def function(source, signature):
    start=source.index(signature)
    opening=source.index('{',start)
    depth=1
    for index in range(opening+1,len(source)):
        depth += (source[index]=='{') - (source[index]=='}')
        if not depth: return source[start:index+1]
    raise AssertionError('unterminated source function')


@unittest.skipUnless(shutil.which('c++') and (SOURCE/'pcsx2/GS/GSState.cpp').is_file(),
                    'native C++ compiler and actual patched PCSX2 source required')
class ProgressiveOutput(unittest.TestCase):
    def test_actual_selector_refresh_and_576_line_output_tables(self):
        source=(SOURCE/'pcsx2/GS/GSState.cpp').read_text(encoding='utf-8')
        header=(SOURCE/'pcsx2/GS/GS.h').read_text(encoding='utf-8')
        enum=re.search(r'enum class GSVideoMode : u8\s*\{.*?\};',header,re.S).group()
        tables='\n'.join(re.search(r'static inline constexpr GSVector4i '+name+
            r'\[\d+\] = \{.*?\};',source,re.S).group() for name in
            ('VideoModeOffsets','VideoModeOffsetsOverscan','VideoModeDividers'))
        methods='\n'.join(function(source,s) for s in (
            'bool GSState::isinterlaced()', 'GSVideoMode GSState::GetVideoMode()',
            'float GSState::GetTvRefreshRate()'))
        code=r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iterator>
using u8=uint8_t;
#define ASSUME(x) do { if (!(x)) __builtin_unreachable(); } while(0)
''' + enum + r'''
struct Registers {
    struct { unsigned CMOD=0,LC=32; } SMODE1;
    struct { unsigned INT=0; } SMODE2;
    struct { unsigned VDP=576; } SYNCV;
};
struct GSState {
    Registers *m_regs;
    bool isinterlaced();
    GSVideoMode GetVideoMode();
    float GetTvRefreshRate();
};
struct { void Error(const char *) {} } Console;
struct GSVector4i {
    int x,y,z,w;
    static constexpr GSVector4i cxpr(int x,int y,int z,int w) { return {x,y,z,w}; }
};
''' + methods + '\n' + tables + r'''
int main() {
    static_assert(int(GSVideoMode::HDTV_1080I)==6);
    static_assert(int(GSVideoMode::SDTV_576P)==7);
    static_assert(std::size(VideoModeOffsets)==7);
    static_assert(std::size(VideoModeOffsetsOverscan)==7);
    static_assert(std::size(VideoModeDividers)==7);
    Registers regs; GSState gs{&regs};
    assert(gs.GetVideoMode()==GSVideoMode::SDTV_576P && gs.GetTvRefreshRate()==50);
    auto index=int(gs.GetVideoMode())-1;
    assert(VideoModeOffsets[index].x==720 && VideoModeOffsets[index].y==576);
    assert(VideoModeOffsetsOverscan[index].y==576);
    assert(VideoModeDividers[index].z==1439 && VideoModeDividers[index].w==575);
    assert(VideoModeDividers[index].y==0);
    regs.SYNCV.VDP=480;
    assert(gs.GetVideoMode()==GSVideoMode::SDTV_480P);
    assert(std::abs(gs.GetTvRefreshRate()-59.94006f)<0.001f);
    assert(VideoModeOffsets[int(gs.GetVideoMode())-1].y==480);
    regs.SMODE1.CMOD=3; regs.SMODE2.INT=1;
    assert(gs.GetVideoMode()==GSVideoMode::PAL && gs.GetTvRefreshRate()==50);
    regs.SMODE1.CMOD=2;
    assert(gs.GetVideoMode()==GSVideoMode::NTSC);
    regs.SMODE1.CMOD=0; regs.SMODE1.LC=22;
    assert(gs.GetVideoMode()==GSVideoMode::HDTV_1080I);
    regs.SMODE2.INT=0;
    assert(gs.GetVideoMode()==GSVideoMode::HDTV_720P);
    regs.SMODE1.LC=7;
    assert(gs.GetVideoMode()==GSVideoMode::VESA);
    regs.SMODE1.CMOD=1;
    assert(gs.GetVideoMode()==GSVideoMode::Unknown);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp); (folder/'video.cpp').write_text(code)
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-fno-pie','-no-pie',str(folder/'video.cpp'),
                '-o',str(folder/'video')],check=True)
            subprocess.run([str(folder/'video')],check=True,timeout=10)


if __name__=='__main__': unittest.main()
