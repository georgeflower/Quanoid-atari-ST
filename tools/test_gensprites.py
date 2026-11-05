import math
import unittest
from unittest.mock import patch

import gensprites


class RotationTests(unittest.TestCase):
    def test_pyramid_enemy_rotates_only_around_y(self):
        angles = []

        def scene(yaw, pitch, x, y, roll=0):
            angles.append((yaw, pitch, roll))
            return None

        with patch.object(gensprites, "make_scene",
                          return_value=(scene, -0.42, math.pi / 2)):
            gensprites.frames_for("pyramid", 1, 8, 1)
        angles = angles[::16]
        self.assertEqual(len(angles), 8)
        self.assertEqual(len({yaw for yaw, pitch, roll in angles}), 8)
        self.assertEqual({pitch for yaw, pitch, roll in angles}, {-0.42})
        self.assertEqual({roll for yaw, pitch, roll in angles}, {0})
        self.assertAlmostEqual(angles[1][0] - angles[0][0], math.pi / 16)

    def test_pyramid_boss_scale_and_unclipped_frames(self):
        with patch.object(gensprites, "render", wraps=gensprites.render) as renderer:
            frames = gensprites.pyramid_boss_frames()
        args = renderer.call_args.args
        self.assertEqual(args[0], 44)
        self.assertAlmostEqual(args[3] / (28 * 0.52), 1.10)
        self.assertAlmostEqual(args[5], 2 * math.pi)
        self.assertEqual(len(frames), 32)
        for rows in frames:
            self.assertTrue(all(pixel == " " for pixel in rows[0] + rows[-1]))
            self.assertTrue(all(row[0] == row[-1] == " " for row in rows))

    def test_boss_rotation_axes(self):
        for kind in ("pyramid", "hex"):
            calls = []

            def scene(yaw, pitch, x, y, roll=0):
                calls.append((yaw, pitch, roll))
                return None

            with patch.object(gensprites, "make_scene", return_value=(scene, -0.4, 1)):
                animated, pitch = gensprites.boss_scene(kind)
            animated(1.2, 0.8, 0, 0)
            self.assertEqual(calls, [(1.2, -0.4, 0 if kind == "pyramid" else 1.2)])
        animated, pitch = gensprites.boss_scene("sphere")
        with patch.object(gensprites, "sphere_hit", return_value=None) as sphere:
            animated(1.2, 0.8, 0, 0)
        self.assertEqual(sphere.call_args.args[:2], (1.2, pitch))
        self.assertEqual(sphere.call_args.kwargs, {"rotating_light": True})

    def test_pyramid_shows_all_sides_but_not_bottom(self):
        planes = gensprites.pyramid_planes(0.80, 1.4)
        _, pitch = gensprites.boss_scene("pyramid")
        seen = set()
        for frame in range(gensprites.BOSS_FRAMES):
            yaw = 2 * math.pi * frame / gensprites.BOSS_FRAMES
            normals = [gensprites.rot(n, yaw, pitch) for n, _ in planes]
            for y in range(-16, 17):
                for x in range(-16, 17):
                    hit = gensprites.solid_hit(planes, yaw, pitch, x / 16, y / 16)
                    if hit:
                        seen.add(normals.index(hit[1]))
        self.assertEqual(seen, {1, 2, 3, 4})

    def test_sphere_highlight_moves(self):
        left = gensprites.sphere_hit(0, -0.3, -0.4, -0.4, 0.92, stripes=False,
                                     rotating_light=True)[1]
        right = gensprites.sphere_hit(0, -0.3, 0.4, -0.4, 0.92, stripes=False,
                                      rotating_light=True)[1]
        rotated_left = gensprites.sphere_hit(math.pi / 2, -0.3, -0.4, -0.4, 0.92,
                                             stripes=False, rotating_light=True)[1]
        rotated_right = gensprites.sphere_hit(math.pi / 2, -0.3, 0.4, -0.4, 0.92,
                                              stripes=False, rotating_light=True)[1]
        self.assertGreater(left, right)
        self.assertGreater(rotated_right, rotated_left)

    def test_both_axes_rotate(self):
        angles = []

        def scene(yaw, pitch, x, y):
            angles.append((yaw, pitch))
            return None

        gensprites.render(1, scene, 8, 1, -0.3, 2 * math.pi)
        angles = angles[::16]
        self.assertEqual(len({yaw for yaw, pitch in angles}), 8)
        self.assertEqual(len({pitch for yaw, pitch in angles}), 8)
        self.assertAlmostEqual(angles[1][0] - angles[0][0], math.pi / 4)
        self.assertAlmostEqual(angles[1][1] - angles[0][1], math.pi / 4)

    def test_sphere_is_round_and_connected(self):
        for size, count, scale in ((12, 8, 0.46), (28, 16, 0.48)):
            frames = gensprites.frames_for("sphere", size, count, size * scale)
            self.assertGreater(len({tuple(rows) for rows in frames}), 2)
            for rows in frames:
                points = {(x, y) for y, row in enumerate(rows)
                          for x, pixel in enumerate(row) if pixel != " "}
                self.assertEqual(points, {(size - 1 - x, y) for x, y in points})
                self.assertEqual(points, {(y, x) for x, y in points})
                self.assertTrue(all(0 < x < size - 1 and 0 < y < size - 1
                                    for x, y in points))
                visited = {next(iter(points))}
                pending = list(visited)
                while pending:
                    x, y = pending.pop()
                    for adjacent in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                        if adjacent in points and adjacent not in visited:
                            visited.add(adjacent)
                            pending.append(adjacent)
                self.assertEqual(visited, points)
            self.assertNotEqual(gensprites.BOSS_TONE["sphere"][0], "f")
            self.assertNotEqual(gensprites.ENEMY_TONE[1][0][0], "f")

    def test_sphere_planar_mask_has_no_extra_pixels(self):
        for rows in gensprites.frames_for("sphere", 12, 8, 6):
            words = gensprites.planar(rows, gensprites.ENEMY_TONE[1][0], 12)
            self.assertEqual(len(words), 60)
            for y, row in enumerate(rows):
                expected = sum(0x8000 >> x for x, pixel in enumerate(row) if pixel != " ")
                self.assertEqual(words[y * 5], expected)
                self.assertTrue(all(word & ~expected == 0 for word in words[y * 5 + 1:y * 5 + 5]))


if __name__ == "__main__":
    unittest.main()
