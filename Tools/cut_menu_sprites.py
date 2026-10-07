"""Cut the vertical main-menu sprite sheet into border pieces.

born2flap-verticalmainmenusprites.png (1536x1024) is laid out as:
  LEFT  (x ~38..699) : the actual cuttable sprites on a TRANSPARENT background —
                        the vertical gold rail, its ornate top fan and bottom
                        taper, and 6 horizontal dividers (thin gold line + a
                        small decorative finial at the left end).
  RIGHT (x ~512..1503): a DARK reference render of the assembled menu (opaque
                        dark background — never cut from here; it is what the
                        previous, wrong `b2f_menu_top` came from).

Correct pieces (verified by pixel/alpha analysis — all dark==0, i.e. clean):
  - left border   : vertical gold rail  x~41..99,  y~18..993
  - top border    : ornate top fan      x~38..104, y~18..62
  - bottom border : ornate bottom taper x~38..104, y~918..995
  - divider       : one horizontal divider (finial + thin gold line)
                    x~135..695, y~150..212
"""
from PIL import Image
import numpy as np
import os


def trim(im, pad=0):
    a = np.array(im)
    al = a[:, :, 3]
    rows = np.where(al.max(axis=1) > 10)[0]
    cols = np.where(al.max(axis=0) > 10)[0]
    if len(rows) == 0:
        return im
    x0 = max(0, int(cols.min()) - pad)
    y0 = max(0, int(rows.min()) - pad)
    x1 = min(im.width, int(cols.max()) + 1 + pad)
    y1 = min(im.height, int(rows.max()) + 1 + pad)
    return im.crop((x0, y0, x1, y1))


def main():
    src = os.path.expanduser(r'~/Downloads/born2flap-verticalmainmenusprites.png')
    out = os.path.expanduser('~/Downloads')
    im = Image.open(src).convert('RGBA')

    pieces = {
        'b2f_menu_left': trim(im.crop((40, 16, 102, 995))),
        'b2f_menu_top': trim(im.crop((36, 16, 106, 64))),
        'b2f_menu_bottom': trim(im.crop((36, 916, 106, 996))),
        'b2f_menu_divider': trim(im.crop((135, 150, 695, 212))),
    }
    for name, img in pieces.items():
        path = os.path.join(out, name + '.png')
        img.save(path)
        print('WROTE', name, img.size, os.path.getsize(path))


if __name__ == '__main__':
    main()
