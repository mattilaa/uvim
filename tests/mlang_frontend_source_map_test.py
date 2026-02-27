#!/usr/bin/env python3
import unittest

from tools.mlang_frontend.source_map import line_char_to_offset, offset_to_line_char


class MlangFrontendSourceMapTest(unittest.TestCase):
    def test_line_char_to_offset_handles_utf16_surrogates(self) -> None:
        # "😀" uses two UTF-16 code units.
        text = "a😀b\n"
        self.assertEqual(line_char_to_offset(text, 0, 0), 0)  # a
        self.assertEqual(line_char_to_offset(text, 0, 1), 1)  # just before 😀
        self.assertEqual(line_char_to_offset(text, 0, 2), 1)  # inside surrogate -> clamp
        self.assertEqual(line_char_to_offset(text, 0, 3), 2)  # after 😀 -> b
        self.assertEqual(line_char_to_offset(text, 0, 4), 3)  # after b

    def test_offset_to_line_char_handles_utf16_surrogates(self) -> None:
        text = "a😀b\n"
        self.assertEqual(offset_to_line_char(text, 0), (0, 0))
        self.assertEqual(offset_to_line_char(text, 1), (0, 1))
        self.assertEqual(offset_to_line_char(text, 2), (0, 3))
        self.assertEqual(offset_to_line_char(text, 3), (0, 4))

    def test_line_out_of_range_clamps_to_end(self) -> None:
        text = "x\ny\n"
        self.assertEqual(line_char_to_offset(text, 99, 0), len(text))
        self.assertEqual(offset_to_line_char(text, 999), (2, 0))


if __name__ == "__main__":
    unittest.main(verbosity=2)
