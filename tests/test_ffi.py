"""C ABI integration: python3 tests/test_ffi.py [build/librin.so] [-v]."""

import ctypes
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from support import ROOT, assert_result, copy_project, fixture_expectations, read_source

LIBRARY = ROOT / "build" / "librin.so"
if __name__ == "__main__" and len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
    LIBRARY = Path(sys.argv.pop(1)).resolve()


class FfiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = ctypes.CDLL(str(LIBRARY))
        string, pointer = ctypes.c_char_p, ctypes.c_void_p
        signatures = {
            "rin_run_file": ([string], pointer), "rin_check_file": ([string], pointer),
            "rin_run_source": ([string, string], pointer), "rin_check_source": ([string, string], pointer),
            "rin_result_status": ([pointer], ctypes.c_int),
            "rin_result_output": ([pointer], string), "rin_result_error": ([pointer], string),
            "rin_result_free": ([pointer], None), "rin_version": ([], string),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(cls.lib, name)
            function.argtypes, function.restype = arguments, result

    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="rin ffi ")
        self.addCleanup(directory.cleanup)
        self.base = copy_project("ffi", directory.name)

    def consume(self, result):
        self.assertTrue(result, "C API unexpectedly returned a null result")
        try:
            return (self.lib.rin_result_status(result),
                    self.lib.rin_result_output(result).decode("utf-8"),
                    self.lib.rin_result_error(result).decode("utf-8"))
        finally:
            self.lib.rin_result_free(result)

    def source(self, filename, base_dir=None, check=False):
        function = self.lib.rin_check_source if check else self.lib.rin_run_source
        return self.consume(function(read_source(self.base / filename).encode("utf-8"),
                                     os.fsencode(base_dir) if base_dir else None))

    def test_source_results(self):
        for name in ("utf8.rin", "unproved.rin", "syntax_error.rin"):
            with self.subTest(fixture=name):
                assert_result(self, self.source(name), self.base / name)

    def test_source_imports(self):
        for project in ("imports", "dotted"):
            with self.subTest(project=project):
                name = f"{project}/main.rin"
                assert_result(self, self.source(name, self.base / project), self.base / name)
                self.assertEqual(self.source(name, self.base / project, check=True), (0, "", ""))

    def test_check_rejects_quoted_and_suffixed_imports(self):
        for name in ("quoted.rin", "suffixed.rin"):
            fixture = self.base / "invalid_imports" / name
            with self.subTest(fixture=name):
                assert_result(self, self.source(fixture, fixture.parent, check=True), fixture)

    def test_null_base_directory_defaults_to_current_directory(self):
        previous = Path.cwd()
        try:
            os.chdir(self.base / "current")
            result = self.source("current/main.rin")
        finally:
            os.chdir(previous)
        assert_result(self, result, self.base / "current/main.rin")

    def test_run_file_and_check_file(self):
        path = self.base / "a file.rin"
        shutil.copyfile(self.base / "unproved.rin", path)
        self.assertEqual(self.consume(self.lib.rin_check_file(os.fsencode(path))), (0, "", ""))
        assert_result(self, self.consume(self.lib.rin_run_file(os.fsencode(path))), path)

    def test_missing_file_returns_an_error_result(self):
        path = self.base / "missing.rin"
        for name in ("rin_run_file", "rin_check_file"):
            with self.subTest(function=name):
                status, output, error = self.consume(getattr(self.lib, name)(os.fsencode(path)))
                self.assertEqual((status, output), (2, ""))
                self.assertIn(str(path), error)

    def test_check_does_not_prove_or_generate_terms(self):
        self.assertEqual(self.source("recursive.rin", check=True), (0, "", ""))

    def test_check_validates_changed_imports(self):
        base = self.base / "current"
        self.assertEqual(self.source("current/check.rin", base, check=True), (0, "", ""))
        shutil.copyfile(self.base / "syntax_error.rin", base / "base.rin")
        status, output, error = self.source("current/check.rin", base, check=True)
        self.assertEqual((status, output), (2, ""))
        self.assertIn(str(base / "base.rin"), error)
        self.assertRegex(error, r":2\b")

    def test_calls_are_isolated_and_results_own_their_text(self):
        first = self.lib.rin_run_source(read_source(self.base / "first.rin").encode("utf-8"), None)
        self.assertTrue(first)
        try:
            self.assertEqual(self.lib.rin_result_status(first), 0)
            assert_result(self, self.source("second.rin"), self.base / "second.rin")
            expected = fixture_expectations(self.base / "first.rin")["stdout"].encode("utf-8")
            self.assertEqual(self.lib.rin_result_output(first), expected)
            self.assertEqual(self.lib.rin_result_error(first), b"")
        finally:
            self.lib.rin_result_free(first)
        assert_result(self, self.source("third.rin"), self.base / "third.rin")

    def test_null_inputs_return_errors_and_free_null_is_safe(self):
        for name, args in (("rin_run_file", (None,)), ("rin_check_file", (None,)),
                           ("rin_run_source", (None, None)), ("rin_check_source", (None, None))):
            with self.subTest(function=name):
                status, output, error = self.consume(getattr(self.lib, name)(*args))
                self.assertEqual((status, output), (2, ""))
                self.assertTrue(error.strip())
        self.lib.rin_result_free(None)

    def test_version_is_nonempty_utf8(self):
        self.assertTrue(self.lib.rin_version().decode("utf-8").strip())


if __name__ == "__main__":
    unittest.main()
