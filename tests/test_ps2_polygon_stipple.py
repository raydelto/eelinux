"""Monochrome polygon patterns compared with pinned Xorg MI geometry."""
from pathlib import Path
from test_ps2_polygon import ROOT,POLYGON_SOURCES,headers as polygon_headers,copy_flags,XORG
SOURCES=POLYGON_SOURCES
CASES=('transparent','opaque','rules','origins','relative','redirected','rgb555','boolean','snapshot','owned','noop','preflight','allocation','budget','failure','cursor','random','rewrap')
def headers(folder):
 polygon_headers(folder)
 (Path(folder)/'previous-polygon.c').write_text((ROOT/'tests/graphics/ps2-polygon.c').read_text().replace('int main(', 'int previous_polygon_main(').replace('int __wrap_ioctl(', 'int polygon_ioctl('))
