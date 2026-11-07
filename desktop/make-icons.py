"""Generate the original one-bit TWM controller-symbol artwork."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT/'rootfs/usr/share/ps2/twm'
TARGET.mkdir(parents=True, exist_ok=True)
shapes = {
    'triangle': lambda x,y: (y == 9 and 1 <= x <= 9) or (abs(x-5) == (y-1)//2 and 1 <= y <= 9),
    'square': lambda x,y: (x in (1,9) and 1 <= y <= 9) or (y in (1,9) and 1 <= x <= 9),
    'cross': lambda x,y: 1 <= x <= 9 and 1 <= y <= 9 and (x == y or x == 10-y),
    'circle': lambda x,y: 13 <= (x-5)**2+(y-5)**2 <= 22,
}
for name, shape in shapes.items():
    rows = [sum(int(shape(x,y)) << x for x in range(11)) for y in range(11)]
    data = [byte for row in rows for byte in (row & 255, row >> 8)]
    text = '#define '+name+'_width 11\n#define '+name+'_height 11\n'
    text += 'static unsigned char '+name+'_bits[] = {\n    '
    text += ', '.join(f'0x{x:02x}' for x in data) + '\n};\n'
    (TARGET/(name+'.xbm')).write_text(text)
