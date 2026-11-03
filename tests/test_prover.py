"""Run with: python3 tests/test_prover.py [path/to/prover] [-v]."""

from pathlib import Path
import sys
import tempfile
import unittest

from support import FIXTURES, ROOT, assert_result, run_cli


BINARY = ROOT / "build" / "rin"
if __name__ == "__main__" and len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
    BINARY = Path(sys.argv.pop(1)).resolve()
PROVER = FIXTURES / "prover"


class ProverTests(unittest.TestCase):
    def test_source_fixtures(self):
        fixtures = sorted((PROVER / "cases").glob("*.rin"))
        self.assertTrue(fixtures)
        for fixture in fixtures:
            with self.subTest(fixture=fixture.stem):
                assert_result(self, run_cli(BINARY, fixture), fixture)

    def test_main_example(self):
        result = run_cli(BINARY, ROOT / ".rin" / "main.rin", cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout,
                         "verify P1 PASS\nverify P2 PASS\nverify P3 PASS\n"
                         "verify P4 PASS\nverify P5 PASS\n")
        if result.stderr:
            self.assertIn("limit", result.stderr.lower())

    def test_unicode_comments_and_crlf(self):
        fixture = PROVER / "unicode_comments_and_crlf.rin"
        content = fixture.read_bytes()
        self.assertIn(b"\r\n", content)
        self.assertNotIn(b"\n", content.replace(b"\r\n", b""))
        assert_result(self, run_cli(BINARY, fixture), fixture)

    def test_missing_file_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            result = run_cli(BINARY, Path(directory) / "missing.rin")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertTrue(result.stderr.strip())


if __name__ == "__main__":
    unittest.main()
