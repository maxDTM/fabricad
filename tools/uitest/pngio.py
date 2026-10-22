"""Minimal PNG reader and writer (stdlib only) for tools/uitest/cvd.py and compare.py.

read_png(path) -> Image(width, height, rows) with rows as lists of (r, g, b) tuples (0..255; alpha is
composited over white). Supports 8- and 16-bit grey, grey+alpha, RGB, RGBA and palette images, non-interlaced
and Adam7. write_png(path, image) writes 8-bit RGB.
SPDX-License-Identifier: LGPL-2.1-or-later
"""
import struct
import zlib


class Image:
    def __init__(self, width, height, rows):
        self.width = width
        self.height = height
        self.rows = rows  # list of lists of (r, g, b)

    def crop(self, x, y, w, h):
        x0, y0 = max(0, int(x)), max(0, int(y))
        x1, y1 = min(self.width, int(x + w)), min(self.height, int(y + h))
        return Image(max(0, x1 - x0), max(0, y1 - y0), [r[x0:x1] for r in self.rows[y0:y1]])

    def pixel(self, x, y):
        return self.rows[int(y)][int(x)]


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(data, width, height, bpp, rowbytes, offset):
    out = []
    prev = bytearray(rowbytes)
    pos = offset
    for _ in range(height):
        ft = data[pos]
        line = bytearray(data[pos + 1:pos + 1 + rowbytes])
        pos += 1 + rowbytes
        if ft == 1:
            for i in range(bpp, rowbytes):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif ft == 2:
            for i in range(rowbytes):
                line[i] = (line[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(rowbytes):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(rowbytes):
                left = line[i - bpp] if i >= bpp else 0
                up_left = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, prev[i], up_left)) & 255
        elif ft != 0:
            raise ValueError("bad PNG filter %d" % ft)
        out.append(line)
        prev = line
    return out, pos


def _samples(line, width, depth, channels):
    if depth == 8:
        return list(line[:width * channels])
    if depth == 16:
        return [line[i] for i in range(0, width * channels * 2, 2)]  # high byte
    vals = []
    per = 8 // depth
    mask = (1 << depth) - 1
    for i in range(width * channels):
        b = line[i // per]
        shift = 8 - depth * (i % per + 1)
        vals.append((b >> shift) & mask)
    return vals


def read_png(path):
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG (convert it first, e.g. magick in.jpg out.png)" % path)
    pos, idat, palette, trns = 8, [], None, None
    while pos < len(raw):
        ln, = struct.unpack(">I", raw[pos:pos + 4])
        typ = raw[pos + 4:pos + 8]
        body = raw[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif typ == b"tRNS":
            trns = body
        elif typ == b"IDAT":
            idat.append(body)
        elif typ == b"IEND":
            break
    data = zlib.decompress(b"".join(idat))
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bits = depth * channels
    bpp = max(1, bits // 8)
    maxv = (1 << depth) - 1 if depth < 8 else 255

    def convert(vals, w):
        px = []
        for i in range(w):
            if ctype == 3:
                idx = vals[i]
                r, g, b = palette[idx]
                a = trns[idx] if trns and idx < len(trns) else 255
            elif ctype in (0, 4):
                v = vals[i * channels] * 255 // maxv
                r = g = b = v
                a = vals[i * channels + 1] if ctype == 4 else 255
            else:
                r, g, b = vals[i * channels:i * channels + 3]
                a = vals[i * channels + 3] if ctype == 6 else 255
            if a < 255:  # composite over white
                r = (r * a + 255 * (255 - a)) // 255
                g = (g * a + 255 * (255 - a)) // 255
                b = (b * a + 255 * (255 - a)) // 255
            px.append((r, g, b))
        return px

    if not interlace:
        lines, _ = _unfilter(data, width, height, bpp, (width * bits + 7) // 8, 0)
        rows = [convert(_samples(l, width, depth, channels), width) for l in lines]
        return Image(width, height, rows)
    rows = [[(0, 0, 0)] * width for _ in range(height)]
    off = 0
    for x0, y0, dx, dy in ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)):
        pw = (width - x0 + dx - 1) // dx
        ph = (height - y0 + dy - 1) // dy
        if pw <= 0 or ph <= 0:
            continue
        lines, off = _unfilter(data, pw, ph, bpp, (pw * bits + 7) // 8, off)
        for j, l in enumerate(lines):
            for i, p in enumerate(convert(_samples(l, pw, depth, channels), pw)):
                rows[y0 + j * dy][x0 + i * dx] = p
    return Image(width, height, rows)


def write_png(path, img):
    def chunk(typ, body):
        c = typ + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    raw = bytearray()
    for row in img.rows:
        raw.append(0)
        for p in row:
            raw.extend(p[:3])
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", img.width, img.height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))
