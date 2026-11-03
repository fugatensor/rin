"""C ABI integration: python3 tests/test_ffi.py [build/libsuka.so] [-v]."""

import ctypes
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from support import ROOT, assert_result, copy_project, fixture_expectations, read_source

LIBRARY = ROOT / "build" / "libsuka.so"
if __name__ == "__main__" and len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
    LIBRARY = Path(sys.argv.pop(1)).resolve()


class FfiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = ctypes.CDLL(str(LIBRARY))
        string, pointer = ctypes.c_char_p, ctypes.c_void_p
        signatures = {
            "suka_run_file": ([string], pointer), "suka_check_file": ([string], pointer),
            "suka_run_source": ([string, string], pointer), "suka_check_source": ([string, string], pointer),
            "suka_result_status": ([pointer], ctypes.c_int),
            "suka_result_output": ([pointer], string), "suka_result_error": ([pointer], string),
            "suka_result_free": ([pointer], None), "suka_version": ([], string),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(cls.lib, name)
            function.argtypes, function.restype = arguments, result

    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="suka ffi ")
        self.addCleanup(directory.cleanup)
        self.base = copy_project("ffi", directory.name)

    def consume(self, result):
        self.assertTrue(result, "C API unexpectedly returned a null result")
        try:
            return (self.lib.suka_result_status(result),
                    self.lib.suka_result_output(result).decode("utf-8"),
                    self.lib.suka_result_error(result).decode("utf-8"))
        finally:
            self.lib.suka_result_free(result)

    def source(self, filename, base_dir=None, check=False):
        function = self.lib.suka_check_source if check else self.lib.suka_run_source
        return self.consume(function(read_source(self.base / filename).encode("utf-8"),
                                     os.fsencode(base_dir) if base_dir else None))

    def test_source_results(self):
        for name in ("utf8.suka", "unproved.suka", "syntax_error.suka"):
            with self.subTest(fixture=name):
                assert_result(self, self.source(name), self.base / name)

    def test_source_imports(self):
        for project in ("imports", "dotted"):
            with self.subTest(project=project):
                name = f"{project}/main.suka"
                assert_result(self, self.source(name, self.base / project), self.base / name)
                self.assertEqual(self.source(name, self.base / project, check=True), (0, "", ""))

    def test_check_rejects_quoted_and_suffixed_imports(self):
        for name in ("quoted.suka", "suffixed.suka"):
            fixture = self.base / "invalid_imports" / name
            with self.subTest(fixture=name):
                assert_result(self, self.source(fixture, fixture.parent, check=True), fixture)

    def test_null_base_directory_defaults_to_current_directory(self):
        previous = Path.cwd()
        try:
            os.chdir(self.base / "current")
            result = self.source("current/main.suka")
        finally:
            os.chdir(previous)
        assert_result(self, result, self.base / "current/main.suka")

    def test_run_file_and_check_file(self):
        path = self.base / "a file.suka"
        shutil.copyfile(self.base / "unproved.suka", path)
        self.assertEqual(self.consume(self.lib.suka_check_file(os.fsencode(path))), (0, "", ""))
        assert_result(self, self.consume(self.lib.suka_run_file(os.fsencode(path))), path)

    def test_missing_file_returns_an_error_result(self):
        path = self.base / "missing.suka"
        for name in ("suka_run_file", "suka_check_file"):
            with self.subTest(function=name):
                status, output, error = self.consume(getattr(self.lib, name)(os.fsencode(path)))
                self.assertEqual((status, output), (2, ""))
                self.assertIn(str(path), error)

    def test_check_does_not_prove_or_generate_terms(self):
        self.assertEqual(self.source("recursive.suka", check=True), (0, "", ""))

    def test_check_validates_changed_imports(self):
        base = self.base / "current"
        self.assertEqual(self.source("current/check.suka", base, check=True), (0, "", ""))
        shutil.copyfile(self.base / "syntax_error.suka", base / "base.suka")
        status, output, error = self.source("current/check.suka", base, check=True)
        self.assertEqual((status, output), (2, ""))
        self.assertIn(str(base / "base.suka"), error)
        self.assertRegex(error, r":2\b")

    def test_calls_are_isolated_and_results_own_their_text(self):
        first = self.lib.suka_run_source(read_source(self.base / "first.suka").encode("utf-8"), None)
        self.assertTrue(first)
        try:
            self.assertEqual(self.lib.suka_result_status(first), 0)
            assert_result(self, self.source("second.suka"), self.base / "second.suka")
            expected = fixture_expectations(self.base / "first.suka")["stdout"].encode("utf-8")
            self.assertEqual(self.lib.suka_result_output(first), expected)
            self.assertEqual(self.lib.suka_result_error(first), b"")
        finally:
            self.lib.suka_result_free(first)
        assert_result(self, self.source("third.suka"), self.base / "third.suka")

    def test_null_inputs_return_errors_and_free_null_is_safe(self):
        for name, args in (("suka_run_file", (None,)), ("suka_check_file", (None,)),
                           ("suka_run_source", (None, None)), ("suka_check_source", (None, None))):
            with self.subTest(function=name):
                status, output, error = self.consume(getattr(self.lib, name)(*args))
                self.assertEqual((status, output), (2, ""))
                self.assertTrue(error.strip())
        self.lib.suka_result_free(None)

    def test_version_is_nonempty_utf8(self):
        self.assertTrue(self.lib.suka_version().decode("utf-8").strip())


if __name__ == "__main__":
    unittest.main()
