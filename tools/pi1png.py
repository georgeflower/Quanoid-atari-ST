import struct, sys
from PIL import Image
def load(fn):
    d = open(fn, 'rb').read()
    pal = struct.unpack('>16H', d[2:34])
    rgb = [(((c >> 8) & 7) * 255 // 7, ((c >> 4) & 7) * 255 // 7, (c & 7) * 255 // 7) for c in pal]
    img = Image.new('RGB', (320, 200))
    px = img.load()
    scr = d[34:34 + 32000]
    for y in range(200):
        for g in range(20):
            o = (y * 20 + g) * 8
            w = struct.unpack('>4H', scr[o:o + 8])
            for b in range(16):
                m = 0x8000 >> b
                c = sum(1 << p for p in range(4) if w[p] & m)
                px[g * 16 + b, y] = rgb[c]
    return img
if __name__ == '__main__':
    files = sys.argv[2:]
    cols = 3
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * 648, rows * 408), (40, 40, 40))
    for i, f in enumerate(files):
        im = load(f).resize((640, 400), Image.NEAREST)
        sheet.paste(im, ((i % cols) * 648 + 4, (i // cols) * 408 + 4))
    sheet.save(sys.argv[1])
