import sys
from PIL import Image
p = sys.argv[1]
im = Image.open(p)
print('size:', im.size, 'mode:', im.mode)
try:
    import pytesseract
    txt = pytesseract.image_to_string(im)
    print('OCR:\n', txt)
except ImportError:
    print('pytesseract not installed')
    # Fallback: dump a coarse ASCII of the image (downscaled luminance)
    im2 = im.convert('L').resize((120, 60))
    px = im2.load()
    for y in range(60):
        line = ''.join('#' if px[x, y] < 128 else '.' for x in range(120))
        print(line)
