#!/usr/bin/env python3
# Generates src/resource/app.ico (clock + green check badge) using only the standard library.
import math, struct, zlib, os, sys

def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))

def sample(x, y, size):
    """Returns (r,g,b,a) 0..255 for normalized point (x,y) in 0..1."""
    small = size <= 24
    out = None
    def over(c):
        nonlocal out
        out = c
    cx = cy = 0.5
    d = math.hypot(x - cx, y - cy)
    if d <= 0.485:
        over((30, 94, 172, 255))                    # blue ring
    if d <= 0.395:
        over((255, 255, 255, 255))                  # face
    if d <= 0.395 and not small:
        for k in range(12):
            a = k * math.pi / 6
            r0 = 0.30 if k % 3 == 0 else 0.34
            r1 = 0.37
            w = 0.022 if k % 3 == 0 else 0.012
            if seg_dist(x, y, cx + r0 * math.sin(a), cy - r0 * math.cos(a),
                        cx + r1 * math.sin(a), cy - r1 * math.cos(a)) <= w:
                over((30, 94, 172, 255))
    if d <= 0.395:
        hw = 0.05 if small else 0.038
        ah = math.radians(-60)                       # hour hand ~10 o'clock
        am = math.radians(60)                        # minute hand ~2 o'clock
        if seg_dist(x, y, cx, cy, cx + 0.19 * math.sin(ah), cy - 0.19 * math.cos(ah)) <= hw:
            over((28, 48, 88, 255))
        if seg_dist(x, y, cx, cy, cx + 0.29 * math.sin(am), cy - 0.29 * math.cos(am)) <= hw * 0.9:
            over((28, 48, 88, 255))
        if d <= 0.045:
            over((28, 48, 88, 255))
    bx, by, br = 0.735, 0.735, 0.235
    db = math.hypot(x - bx, y - by)
    if db <= br + 0.03:
        over((255, 255, 255, 255))                   # white halo for contrast
    if db <= br:
        over((36, 158, 68, 255))
        cw = 0.045 if not small else 0.06
        if (seg_dist(x, y, bx - 0.10, by + 0.005, bx - 0.028, by + 0.075) <= cw or
                seg_dist(x, y, bx - 0.028, by + 0.075, bx + 0.11, by - 0.075) <= cw):
            over((255, 255, 255, 255))
    return out

def render(size):
    ss = 4 if size >= 32 else 6
    rows = []
    for py in range(size):
        row = []
        for px in range(size):
            sr = sg = sb = sa = 0
            for j in range(ss):
                for i in range(ss):
                    c = sample((px + (i + 0.5) / ss) / size, (py + (j + 0.5) / ss) / size, size)
                    if c:
                        sr += c[0]; sg += c[1]; sb += c[2]; sa += 255
            n = ss * ss
            if sa == 0:
                row.append((0, 0, 0, 0))
            else:
                cnt = sa // 255
                row.append((sr // cnt, sg // cnt, sb // cnt, sa // n))
        rows.append(row)
    return rows

def png(rows, size):
    raw = b''.join(b'\x00' + b''.join(struct.pack('BBBB', *p) for p in r) for r in rows)
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def bmp(rows, size):
    hdr = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    px = b''.join(b''.join(struct.pack('BBBB', p[2], p[1], p[0], p[3]) for p in r) for r in reversed(rows))
    stride = ((size + 31) // 32) * 4
    return hdr + px + b'\x00' * (stride * size)

def main():
    out = sys.argv[1]
    sizes = [16, 24, 32, 48, 64, 256]
    imgs = []
    for s in sizes:
        rows = render(s)
        imgs.append((s, png(rows, s) if s == 256 else bmp(rows, s)))
    data = struct.pack('<HHH', 0, 1, len(imgs))
    off = 6 + 16 * len(imgs)
    body = b''
    for s, d in imgs:
        data += struct.pack('<BBBBHHII', 0 if s == 256 else s, 0 if s == 256 else s, 0, 0, 1, 32, len(d), off + len(body))
        body += d
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(data + body)
    print('wrote', out, len(data + body), 'bytes')

main()
