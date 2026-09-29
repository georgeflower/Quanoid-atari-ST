#!/usr/bin/env python3
"""Qarkanoid ST sound effects -> src/sfx.h

The effects follow the Web Audio definitions in the web game's src/utils/sounds.ts
(frequencies, sweeps, gains, durations) and are rendered for the YM2149 at 50 Hz:
  - every oscillator becomes a YM square wave (sine/triangle get one volume step less)
  - low sawtooth rumbles and filtered sizzles become YM noise
  - gain -> YM volume on a log scale (~2 dB per step, 0.35 = full volume)
  - exponentialRampToValueAtTime(0.01) -> linear-in-dB volume decay
Power-up pickups use MP3 samples in the web game; those have no synth description,
so they get short chip jingles designed around the power-up names.

Frame data: two words per 1/50 s
  w0 = tone period (12 bit) | 0x1000 tone on | 0x2000 noise on
  w1 = volume << 8 | noise period
"""
import math
import sys

FPS = 50
YM_CLOCK = 125000.0          # 2 MHz / 16


def frames(sec):
    return max(1, int(math.ceil(sec * FPS - 1e-9)))


def period(freq):
    return max(1, min(4095, int(round(YM_CLOCK / max(freq, 1.0)))))


def vol(gain, soft):
    if gain <= 0.0005:
        return 0
    v = 15 + 20 * math.log10(gain / 0.35) / 2.0
    if soft:
        v -= 1
    return max(0, min(15, int(round(v))))


class Layer:
    """One oscillator: start (s), duration (s), freq sweep, gain envelope."""

    def __init__(self, start, dur, f0, f1=None, sweep="exp", gain=0.1, wave="sine",
                 env="decay", noise=None, steps=None, attack=0.0):
        self.start, self.dur = start, dur
        self.f0, self.f1 = f0, (f1 if f1 is not None else f0)
        self.sweep, self.gain, self.wave, self.env = sweep, gain, wave, env
        self.noise = noise           # (np0, np1) noise period sweep, or None
        self.steps = steps           # [(t, freq)] step sequence
        self.attack = attack

    def render(self):
        out = [None] * frames(self.start + self.dur)
        n = frames(self.dur)
        s0 = int(round(self.start * FPS))
        soft = self.wave in ("sine", "triangle")
        for i in range(n):
            t = i / FPS
            u = min(1.0, i / max(1, n - 1)) if n > 1 else 0.0
            if self.steps:
                f = self.steps[0][1]
                for st, sf in self.steps:
                    if t >= st - 1e-9:
                        f = sf
            elif self.sweep == "exp":
                f = self.f0 * (self.f1 / self.f0) ** u
            elif self.sweep == "lin":
                f = self.f0 + (self.f1 - self.f0) * u
            elif self.sweep == "warble":
                f = self.f0 if i % 2 == 0 else self.f1
            else:
                f = self.f0
            if self.env == "decay":
                g = self.gain * (0.01 / self.gain) ** u if self.gain > 0.01 else self.gain
            elif self.env == "hold":            # fanfare notes: attack, hold, short release
                rel = max(1, frames(0.05))
                g = self.gain if i < n - rel else self.gain * (0.01 / self.gain) ** ((i - (n - rel) + 1) / rel)
            elif self.env == "swell":           # rise to peak at 40 %, then fade
                g = self.gain * (0.3 + 0.7 * (u / 0.4)) if u < 0.4 else self.gain * (0.01 / self.gain) ** ((u - 0.4) / 0.6)
            else:
                g = self.gain
            if self.attack and t < self.attack:
                g *= (t + 1 / FPS) / (self.attack + 1 / FPS)
            w0 = period(f)
            w1 = vol(g, soft) << 8
            if self.wave != "noise":
                w0 |= 0x1000
            if self.noise:
                n0, n1 = self.noise
                w0 |= 0x2000
                w1 |= max(1, min(31, int(round(n0 + (n1 - n0) * u))))
            if s0 + i < len(out):
                out[s0 + i] = (w0, w1)
        return [x if x else (0, 0) for x in out]


L = Layer
SOUNDS = [
    # name, priority, layers
    # playBounce: 200 Hz sine, 0.06 -> 0.01 over 0.1 s
    ("BOUNCE", 1, [L(0, 0.10, 200, gain=0.06)]),
    # playBrickHit default: 800 Hz sine, 0.092, 0.05 s (combo pitch applied at run time)
    ("BRICK", 2, [L(0, 0.05, 800, gain=0.092)]),
    # playBrickHit cracked, 3 hits left: 500 Hz sine 0.08, 0.04 s
    ("CRACK3", 2, [L(0, 0.04, 500, gain=0.08)]),
    # cracked, 2 left: 700 Hz triangle 0.10, 0.06 s
    ("CRACK2", 2, [L(0, 0.06, 700, gain=0.10, wave="triangle")]),
    # cracked, final: 1100 -> 300 triangle 0.13, 0.12 s
    ("CRACK1", 2, [L(0, 0.12, 1100, 300, gain=0.13, wave="triangle")]),
    # metal: no own sound in the web game, it bounces
    # playExplosiveBrickSound: bass saw 80->20 (0.25, 0.6 s) + square 300->50 (0.15, 0.3 s) + sizzle
    ("EXPLODE", 5, [L(0, 0.6, 80, 40, gain=0.25, wave="noise", noise=(6, 31)),
                    L(0, 0.3, 300, 50, gain=0.15, wave="square")]),
    # playShoot: saw 800 -> 100, 0.15, 0.1 s
    ("SHOOT", 2, [L(0, 0.10, 800, 100, gain=0.15, wave="saw")]),
    # playLoseLife: saw 400 -> 50, 0.3, 0.5 s
    ("LOSELIFE", 6, [L(0, 0.5, 400, 50, gain=0.3, wave="saw")]),
    # playFailureSound (game over): saw 400/350/300 then ->80, square 200->40, 0.25, 0.6 s
    ("GAMEOVER", 7, [L(0, 0.6, 400, 80, gain=0.25, wave="saw",
                       steps=[(0, 400), (0.15, 350), (0.3, 300), (0.4, 200), (0.5, 120)]),
                     L(0, 0.6, 200, 40, gain=0.25, wave="square")]),
    # playLevelClearFanfare: C5 E5 G5 (0.25 s each, 0.1 s apart) + C6 held 0.5 s, sub thud 120->40
    ("LEVELCLEAR", 7, [L(0, 0.8, 523, gain=0.16, env="hold",
                         steps=[(0, 523), (0.1, 659), (0.2, 784), (0.3, 1047)]),
                       L(0, 0.15, 120, 40, gain=0.18)]),
    # playWin (new high score): C E G C, 0.1 s apart, 0.3 s each
    ("WIN", 7, [L(0, 0.6, 262, gain=0.2, env="hold",
                  steps=[(0, 262), (0.1, 330), (0.2, 392), (0.3, 523)]),
                L(0.3, 0.3, 523, gain=0.2)]),
    # playSecondChanceSaveSound: zap saw 1500->300 (0.3, 0.15 s) + relief 400->800 at +0.1 s
    ("BARRIERSAVE", 5, [L(0, 0.15, 1500, 300, gain=0.3, wave="saw"),
                        L(0.1, 0.2, 400, 800, gain=0.2)]),
    # playMenuClick: 800 Hz 0.15, 0.05 s
    ("MENUCLICK", 3, [L(0, 0.05, 800, gain=0.15)]),
    # playMenuHover: 400 Hz 0.08, 0.04 s
    ("MENUHOVER", 3, [L(0, 0.04, 400, gain=0.08)]),
    # --- power-up pickups (web game plays samples; chip jingles here) ---
    ("PU_MULTI", 4, [L(0, 0.24, 600, gain=0.3, steps=[(0, 600), (0.06, 900), (0.12, 1200), (0.18, 1600)]),
                     L(0.06, 0.24, 300, gain=0.15, steps=[(0, 300), (0.06, 450), (0.12, 600), (0.18, 800)])]),
    ("PU_TURRET", 4, [L(0, 0.06, 2000, gain=0.3, wave="noise", noise=(2, 2)),
                      L(0.04, 0.26, 200, gain=0.3, wave="square",
                        steps=[(0, 200), (0.06, 300), (0.12, 400), (0.18, 800)])]),
    ("PU_FIRE", 4, [L(0, 0.35, 150, 600, gain=0.3, wave="saw"),
                    L(0, 0.35, 150, gain=0.2, wave="noise", noise=(20, 3), env="swell")]),
    ("PU_SLOW", 4, [L(0, 0.35, 800, 200, gain=0.3)]),
    ("PU_EXTEND", 4, [L(0, 0.25, 300, 900, gain=0.3), L(0.05, 0.2, 450, 1350, gain=0.12)]),
    ("PU_SHRINK", 4, [L(0, 0.25, 900, 300, gain=0.3), L(0.05, 0.2, 1350, 450, gain=0.12)]),
    ("PU_SHIELD", 4, [L(0, 0.3, 500, 700, sweep="warble", gain=0.3)]),
    ("PU_BARRIER", 4, [L(0, 0.2, 1000, 2000, gain=0.3, wave="saw"), L(0, 0.3, 400, gain=0.15)]),
    ("PU_LIFE", 4, [L(0, 0.45, 523, gain=0.14, env="hold",
                      steps=[(0, 523), (0.08, 659), (0.16, 784), (0.24, 1047)]),
                    L(0.24, 0.2, 1568, gain=0.06)]),
    # generic playPowerUp: 300 -> 600 sine 0.3, 0.2 s
    ("POWERUP", 4, [L(0, 0.2, 300, 600, gain=0.3)]),
    # --- boss sounds ---
    # playBossHitSound: square 200 -> 50, 0.224, 0.2 s
    ("BOSSHIT", 5, [L(0, 0.2, 200, 50, gain=0.224, wave="square")]),
    # playBossDefeatSound: three saws 400/300/200 -> 50, 0.5 each, 0.1 s apart
    ("BOSSDEFEAT", 8, [L(0, 0.6, 400, 50, gain=0.5, wave="saw"),
                       L(0.1, 0.5, 300, 50, gain=0.5, wave="saw", noise=(10, 31))]),
    # playLaserChargingSound: saw 100 -> 800 over 0.8 s, swelling
    ("LASERCHARGE", 6, [L(0, 0.8, 100, 800, gain=0.125, wave="saw", env="swell")]),
    # playSuperAttackChargingSound: bass 60->120 + square siren 200->1200
    ("SUPERCHARGE", 6, [L(0, 0.6, 120, 240, gain=0.1, env="swell"),
                        L(0, 0.6, 200, 1200, gain=0.05, wave="square", env="swell")]),
    # playPyramidBulletSound: saw 1200 -> 400, 0.12, 0.15 s
    ("BOSSSHOT", 3, [L(0, 0.15, 1200, 400, gain=0.12, wave="saw")]),
    # playBossPhaseTransitionSound: sine 300 -> 600, 0.4, 0.3 s
    ("BOSSPHASE", 7, [L(0, 0.3, 300, 600, gain=0.4)]),
    # playBossIntroSound: the web game plays a siren sample; chip siren here
    ("BOSSINTRO", 7, [L(0, 1.5, 400, 900, sweep="warble", gain=0.3, wave="square"),
                      L(0, 1.5, 120, gain=0.15)]),
    # playBombDropSound: 800 -> 200, 0.15, 0.1 s
    ("BOMBDROP", 3, [L(0, 0.1, 800, 200, gain=0.15)]),
    ("ENEMYSPAWN", 3, [L(0, 0.2, 300, 900, gain=0.12, wave="square")]),
    ("ENEMYHIT", 3, [L(0, 0.08, 900, 400, gain=0.12, wave="square")]),
    ("ENEMYDIE", 4, [L(0, 0.3, 400, 100, gain=0.2, wave="noise", noise=(4, 24)),
                     L(0, 0.2, 600, 150, gain=0.15, wave="square")]),
    # laser firing
    ("LASERFIRE", 5, [L(0, 0.5, 900, 300, gain=0.2, wave="saw", noise=(3, 12))]),
]


def main(dst):
    data, layers, first, nlay, prio = [], [], [], [], []
    for name, pr, lays in SOUNDS:
        assert 1 <= len(lays) <= 2, name
        first.append(len(layers))
        nlay.append(len(lays))
        prio.append(pr)
        for lay in lays:
            fr = lay.render()
            layers.append((len(data) // 2, len(fr)))
            for w0, w1 in fr:
                data += [w0, w1]
    # Flattened single-channel versions for TOS's own Dosound player: one
    # register/value stream per sound, 1/50 s per step. Used by the build that
    # lets TOS drive the sound chip instead of writing the registers directly.
    ds, ds_off = [], []
    for name, pr, lays in SOUNDS:
        ds_off.append(len(ds))
        rendered = [lay.render() for lay in lays]
        n = max(len(r) for r in rendered)
        prev = None
        for i in range(n):
            best = None
            for r in rendered:
                if i < len(r) and r[i][0]:
                    if best is None or (r[i][1] >> 8) > (best[1] >> 8):
                        best = r[i]
            if best is None:
                continue
            w0, w1 = best
            per, vol, npr = w0 & 0xfff, w1 >> 8, w1 & 31
            mix = 0xc0 | 0x3e | (0x08 if not (w0 & 0x2000) else 0)   # channel A only
            if w0 & 0x1000:
                mix &= ~0x01
            step = [0x00, per & 0xff, 0x01, (per >> 8) & 0x0f, 0x08, vol]
            if w0 & 0x2000:
                step += [0x06, npr or 1]
            if mix != prev:
                step += [0x07, mix]
                prev = mix
            step += [0xff, 0x01]
            ds += step
        ds += [0x08, 0x00, 0xff, 0x00]        # silence channel A and stop
    with open(dst, "w") as f:
        f.write("/* generated by tools/gensfx.py - do not edit */\n")
        for i, (name, _, _) in enumerate(SOUNDS):
            f.write("#define SFX_%s %d\n" % (name, i))
        f.write("#define NUM_SFX %d\n" % len(SOUNDS))
        f.write("static const u16 sfx_data[] = {\n")
        for i in range(0, len(data), 12):
            f.write("  " + ",".join("0x%04x" % w for w in data[i:i + 12]) + ",\n")
        f.write("};\n")
        f.write("static const u16 sfx_layer[][2] = {\n")
        f.write(",\n".join("  {%d,%d}" % l for l in layers) + "\n};\n")
        f.write("static const u8 sfx_first[NUM_SFX] = {%s};\n" % ",".join(map(str, first)))
        f.write("static const u8 sfx_nlayers[NUM_SFX] = {%s};\n" % ",".join(map(str, nlay)))
        f.write("static const u8 sfx_prio[NUM_SFX] = {%s};\n" % ",".join(map(str, prio)))
        f.write("static const char *const sfx_name[NUM_SFX] = {%s};\n" %
                ",".join('"%s"' % n for n, _, _ in SOUNDS))
        f.write("static const char sfx_ds[] = {\n")
        for i in range(0, len(ds), 16):
            f.write("  " + ",".join("%d" % (b - 256 if b > 127 else b) for b in ds[i:i + 16]) + ",\n")
        f.write("};\n")
        f.write("static const unsigned short sfx_ds_off[NUM_SFX] = {%s};\n" % ",".join(map(str, ds_off)))
    print("sfx: %d sounds, %d frames, %d bytes" % (len(SOUNDS), len(data) // 2, len(data) * 2))


if __name__ == "__main__":
    main(sys.argv[1])
