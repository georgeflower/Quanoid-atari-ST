import math
import unittest

import gensprites


class RotationTests(unittest.TestCase):
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
