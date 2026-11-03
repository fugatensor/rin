"""CLI/import integration: python3 tests/test_cli.py [build/suka] [-v]."""

from pathlib import Path
import sys
import tempfile
import unittest

from support import ROOT, assert_result, copy_project, read_source, run_cli

BINARY = ROOT / "build" / "suka"
if __name__ == "__main__" and len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
    BINARY = Path(sys.argv.pop(1)).resolve()


class CliTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="suka integration ")
        self.addCleanup(directory.cleanup)
        self.base = Path(directory.name)

    def invoke(self, *arguments, **options):
        return run_cli(BINARY, *arguments, **options)

    def entry(self, project, filename="main.suka"):
        return copy_project("cli/" + project, self.base) / filename

    def assert_fixture(self, path, *arguments, **options):
        result = self.invoke(*arguments, path, **options)
        assert_result(self, result, path)
        return result

    def assert_invalid_group(self, pattern, count):
        self.entry("invalid_imports")
        paths = sorted(self.base.glob(pattern))
        self.assertEqual(len(paths), count)
        for path in paths:
            with self.subTest(fixture=path.name):
                self.assert_fixture(path)

    def test_no_arguments_prints_usage(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("usage", result.stderr.lower())

    def test_invalid_command_line_is_an_error(self):
        for arguments in (("--unknown",), ("--check",), ("a.suka", "b.suka")):
            with self.subTest(arguments=arguments):
                result = self.invoke(*arguments)
                self.assertEqual(result.returncode, 2)
                self.assertTrue(result.stderr.strip())

    def test_examples_run_from_an_unrelated_working_directory(self):
        for filename, labels in (("mother.suka", ("M1", "M2")),
                                 ("nat.suka", ("N0", "N1", "N2"))):
            with self.subTest(filename=filename):
                result = self.invoke(ROOT / ".suka" / filename, cwd=self.base)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "".join(f"verify {label} PASS\n" for label in labels))
                if result.stderr:
                    self.assertIn("limit", result.stderr.lower())

    def test_imports_are_relative_to_the_source_with_space_paths(self):
        self.assert_fixture(self.entry("relative", "main file.suka"), cwd=ROOT)

    def test_imported_queries_use_all_facts_and_preserve_expansion_order(self):
        self.assert_fixture(self.entry("expansion"))

    def test_duplicate_and_diamond_imports_expand_each_file_once(self):
        self.assert_fixture(self.entry("diamond"))

    def test_import_cycles_are_errors(self):
        self.assert_fixture(self.entry("cycle", "first.suka"))

    def test_import_paths_must_be_same_directory_basenames(self):
        self.assert_invalid_group("path_*.suka", 8)

    def test_utf8_and_dotted_module_names(self):
        self.assert_fixture(self.entry("utf8"), cwd=ROOT)

    def test_quoted_imports_explain_how_to_remove_quotes(self):
        self.assert_invalid_group("quote_*.suka", 3)

    def test_explicit_extension_is_rejected_without_appending_it_again(self):
        result = self.assert_fixture(self.entry("invalid_imports", "extension.suka"))
        self.assertNotIn("facts.suka.suka", result.stderr)

    def test_import_requires_exactly_one_module_name(self):
        self.assert_invalid_group("count_*.suka", 4)

    def test_missing_import_reports_the_importing_file(self):
        entry = self.entry("missing")
        result = self.assert_fixture(entry)
        self.assertIn(str(entry), result.stderr)

    def test_import_syntax_errors_report_original_path_and_line(self):
        result = self.assert_fixture(self.entry("broken"))
        self.assertIn(str(self.base / "broken.suka"), result.stderr)

    def test_check_parses_without_running_recursive_inference_or_queries(self):
        self.assert_fixture(self.entry("check", "recursive.suka"), "--check")

    def test_check_stdin_resolves_imports_from_base_directory(self):
        entry = self.entry("stdin")
        result = self.invoke("--check", "--stdin", "--base-dir", self.base,
                             source=read_source(entry), cwd=ROOT)
        assert_result(self, result, entry)

    def test_check_stdin_still_reports_invalid_imported_source(self):
        entry = self.entry("stdin_broken")
        result = self.invoke("--check", "--stdin", "--base-dir", self.base,
                             source=read_source(entry))
        assert_result(self, result, entry)
        self.assertIn(str(self.base / "broken.suka"), result.stderr)


if __name__ == "__main__":
    unittest.main()
