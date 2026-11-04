#!/usr/bin/env python3
"""Pre-rendered, shaded, dithered rotation frames for enemies and bosses
(src/sprites.h).

Each shape is a small set of convex solids (or a textured sphere) that is
ray-cast orthographically with 4x4 supersampling, lit by one directional
light, then reduced to 4 tones with ordered dithering. Tone digits:
  0 outline   1 shadow   2 body   3 highlight      ' ' = transparent
The game maps tones to palette entries per shape (see build_enemy_sprites).
"""
import math, sys

LIGHT = (-0.45, -0.60, 0.66)
_l = math.sqrt(sum(c * c for c in LIGHT))
LIGHT = tuple(c / _l for c in LIGHT)
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def rot(v, yaw, pitch):
    x, y, z = v
    c, s = math.cos(yaw), math.sin(yaw)
    x, z = x * c + z * s, -x * s + z * c
    c, s = math.cos(pitch), math.sin(pitch)
    y, z = y * c - z * s, y * s + z * c
    return (x, y, z)


def box(hx, hy, hz):
    return [((1, 0, 0), hx), ((-1, 0, 0), hx), ((0, 1, 0), hy),
            ((0, -1, 0), hy), ((0, 0, 1), hz), ((0, 0, -1), hz)]


def pyramid_planes(base, height):
    # base on y=+h/2, apex at y=-h/2; side planes through apex and base edges
    hh = height / 2.0
    planes = [((0, 1, 0), hh)]
    for nx, nz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        # point on plane: (nx*base, hh, nz*base); apex (0,-hh,0)
        # normal ~ (nx*height, -base, nz*height) normalised
        n = (nx * height, -base, nz * height)
        ln = math.sqrt(sum(c * c for c in n))
        n = tuple(c / ln for c in n)
        d = n[0] * 0 + n[1] * (-hh) + n[2] * 0       # apex lies on the plane
        planes.append((n, d))
    return planes


def hexprism(radius, half_h):
    planes = [((0, 1, 0), half_h), ((0, -1, 0), half_h)]
    for k in range(6):
        a = math.pi / 6 + k * math.pi / 3
        planes.append(((math.cos(a), 0, math.sin(a)), radius * math.cos(math.pi / 6)))
    return planes


def bipyramid(r, w):
    """Thin octahedron-like spike along +-x: planes |y|*r + |x|*w ... """
    planes = []
    for sx in (1, -1):
        for sy in (1, -1):
            for sz in (1, -1):
                n = (sx / r, sy / w, sz / w)
                ln = math.sqrt(sum(c * c for c in n))
                planes.append((tuple(c / ln for c in n), 1 / ln))
    return planes


def spike(axis, r, w):
    pl = bipyramid(r, w)
    out = []
    for n, d in pl:
        if axis == 1:
            n = (n[1], n[0], n[2])
        elif axis == 2:
            n = (n[2], n[1], n[0])
        out.append((n, d))
    return out


def solid_hit(planes, yaw, pitch, x, y):
    zmax, face = 1e9, None
    zmin = -1e9
    for n, d in planes:
        nx, ny, nz = rot(n, yaw, pitch)
        r = d - nx * x - ny * y
        if abs(nz) < 1e-9:
            if r < 0:
                return None
            continue
        z = r / nz
        if nz > 0:
            if z < zmax:
                zmax, face = z, (nx, ny, nz)
        else:
            if z > zmin:
                zmin = z
    if face is None or zmax < zmin:
        return None
    return zmax, face


def shade_face(n, base=0.0):
    nx, ny, nz = n
    b = nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2]
    return max(0.0, min(1.0, 0.10 + 0.90 * max(b, 0.0) ** 1.5 + base))


def sphere_hit(yaw, pitch, x, y, r, stripes=True):
    q = x * x + y * y
    if q > r * r:
        return None
    z = math.sqrt(r * r - q)
    n = (x / r, y / r, z / r)
    b = shade_face(n)
    # specular glint
    hx, hy, hz = LIGHT[0], LIGHT[1], LIGHT[2] + 1
    ln = math.sqrt(hx * hx + hy * hy + hz * hz)
    spec = max(0.0, (n[0] * hx + n[1] * hy + n[2] * hz) / ln) ** 14
    if stripes:
        # un-rotate the normal into object space and band it by longitude
        c, s = math.cos(-pitch), math.sin(-pitch)
        ny, nz = n[1] * c - n[2] * s, n[1] * s + n[2] * c
        c, s = math.cos(-yaw), math.sin(-yaw)
        nx, nz = n[0] * c + nz * s, -n[0] * s + nz * c
        lon = math.atan2(nx, nz)
        if abs(math.sin(lon * 2)) < 0.34:
            b = max(0.0, b - 0.42)
        lat = math.asin(max(-1, min(1, ny)))
        if abs(lat) < 0.16:
            b = max(0.0, b - 0.35)
    return z, min(1.0, b + 0.9 * spec)


def render(size, scene, frames, scale, pitch, period, outline=True):
    """scene(yaw, x, y) -> (z, brightness) or None; returns list of frames."""
    out = []
    half = size / 2.0
    for f in range(frames):
        yaw = period * f / frames
        cover = [[0] * size for _ in range(size)]
        light = [[0.0] * size for _ in range(size)]
        for py in range(size):
            for px in range(size):
                n = 0
                acc = 0.0
                for sy in range(4):
                    for sx in range(4):
                        x = (px + (sx + 0.5) / 4 - half) / scale
                        y = (py + (sy + 0.5) / 4 - half) / scale
                        h = scene(yaw, pitch, x, y)
                        if h:
                            n += 1
                            acc += h[1]
                cover[py][px] = n
                light[py][px] = acc / n if n else 0.0
        rows = []
        for py in range(size):
            row = ""
            for px in range(size):
                if cover[py][px] < 8:
                    row += " "
                    continue
                if outline:
                    edge = False
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        qx, qy = px + dx, py + dy
                        if not (0 <= qx < size and 0 <= qy < size) or cover[qy][qx] < 8:
                            edge = True
                    if edge:
                        row += "0"
                        continue
                p = 1 + light[py][px] * 2.0
                lo = int(p)
                frac = round((p - lo) * 2) / 2.0        # 0, 1/2 or 1: clean checkerboard
                if frac == 1.0:
                    t = lo + 1
                elif frac == 0.5:
                    t = lo + ((px + py) & 1)
                else:
                    t = lo
                row += str(max(1, min(3, t)))
            rows.append(row)
        out.append(rows)
    return out


def solids_scene(solids):
    def scene(yaw, pitch, x, y):
        best = None
        for planes, ox, oy, oz in solids:
            h = solid_hit(planes, yaw, pitch, x - ox, y - oy)
            if h and (best is None or h[0] + oz > best[0]):
                best = (h[0] + oz, shade_face(h[1]))
        return best
    return scene


def make_scene(kind):
    if kind == "cube":
        return solids_scene([(box(0.62, 0.62, 0.62), 0, 0, 0)]), -0.50, math.pi / 2
    if kind == "pyramid":
        return solids_scene([(pyramid_planes(0.80, 1.4), 0, 0.05, 0)]), -0.42, math.pi / 2
    if kind == "hex":
        return solids_scene([(hexprism(0.80, 0.46), 0, 0, 0)]), -0.62, math.pi / 3
    if kind == "star":
        return solids_scene([(spike(0, 1.0, 0.50), 0, 0, 0),
                             (spike(1, 1.0, 0.50), 0, 0, 0),
                             (spike(2, 1.0, 0.50), 0, 0, 0)]), -0.45, math.pi / 2
    if kind == "sphere":
        def scene(yaw, pitch, x, y):
            return sphere_hit(yaw, pitch, x, y, 0.92)
        return scene, -0.30, math.pi
    raise ValueError(kind)


BOSS_TONE = {"cube": "fb2a", "sphere": "f7dc", "pyramid": "f74c", "hex": "fa8c"}
# per enemy slot (cube, sphere, pyramid, unused, star): three variants each
ENEMY_TONE = [["fb2a"] * 3, ["f7dc"] * 3, ["f74c", "f9ec", "fb2a"], None, ["fa8c"] * 3]
ENEMY_KIND = ["cube", "sphere", "pyramid", "star", "star"]


def planar(rows, tone, size):
    """rows of ' ' or '0'..'3' -> per row, per 16px group: mask, p0..p3"""
    ng = (size + 15) >> 4
    out = []
    for row in rows:
        for g in range(ng):
            w = [0] * 5
            for c in range(g * 16, min(size, g * 16 + 16)):
                ch = row[c]
                if ch not in "0123":
                    continue
                col = int(tone[int(ch)], 16)
                bit = 0x8000 >> (c & 15)
                w[0] |= bit
                for pl in range(4):
                    if (col >> pl) & 1:
                        w[1 + pl] |= bit
            out += w
    return out


def emit_words(f, name, dims, data):
    def nest(ds, off):
        if len(ds) == 1:
            return "{" + ",".join("0x%04x" % w for w in data[off:off + ds[0]]) + "}"
        step = 1
        for d in ds[1:]:
            step *= d
        return "{\n" + ",\n".join(nest(ds[1:], off + i * step) for i in range(ds[0])) + "}"
    f.write("static const u16 %s%s = %s;\n" % (name, "".join("[%d]" % d for d in dims), nest(dims, 0)))


def frames_for(kind, size, frames, scale):
    scene, pitch, period = make_scene(kind)
    return render(size, scene, frames, scale, pitch, period)


def emit_boss(f):
    data = []
    for kind in ["cube", "sphere", "pyramid", "hex"]:
        for rows in frames_for(kind, 28, 16, 28 * 0.52):
            data += planar(rows, BOSS_TONE[kind], 28)
    emit_words(f, "gfxd_boss", [4, 16, 28 * 2 * 5], data)


def emit_enemy(f):
    scale = {"cube": 0.60, "sphere": 0.54, "pyramid": 0.52, "star": 0.50}
    data = []
    for t in range(5):
        if ENEMY_TONE[t] is None:
            data += [0] * (8 * 3 * 60)
            continue
        kind = ENEMY_KIND[t]
        fr = frames_for(kind, 12, 8, 12 * scale[kind])
        for rows in fr:
            for v in range(3):
                data += planar(rows, ENEMY_TONE[t][v], 12)
    emit_words(f, "gfxd_enemy", [5, 8, 3, 60], data)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "src/sprites.h"
    with open(out, "w") as f:
        f.write("/* generated by tools/gensprites.py - do not edit */\n")
        f.write("/* planar sprite data: per row, per 16px group: mask, p0..p3 */\n")
        emit_enemy(f)
        emit_boss(f)


if __name__ == "__main__":
    main()
