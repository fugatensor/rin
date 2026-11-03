"""Shared runners; Rin test inputs live in fixtures/, never in Python strings."""

from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = Path(__file__).resolve().parent / "fixtures"


def fixture_path(path):
    path = Path(path)
    return path if path.is_absolute() else FIXTURES / path


def read_source(path):
    return fixture_path(path).read_bytes().decode("utf-8")


def copy_project(relative, destination):
    shutil.copytree(fixture_path(relative), destination, dirs_exist_ok=True)
    return Path(destination)


def run_cli(binary, *arguments, source=None, cwd=None):
    return subprocess.run(
        [str(binary), *(str(argument) for argument in arguments)],
        input=source, cwd=cwd, capture_output=True, text=True,
        encoding="utf-8", timeout=5,
    )


def fixture_expectations(path):
    expected = {"status": 0, "stdout": "", "stderr_pattern": None}
    for line in read_source(path).splitlines():
        if line.startswith("# expect-status:"):
            expected["status"] = int(line.split(":", 1)[1])
        elif line.startswith("# expect-output:"):
            expected["stdout"] += line.split(":", 1)[1].removeprefix(" ") + "\n"
        elif line.startswith("# expect-error:"):
            expected["stderr_pattern"] = line.split(":", 1)[1].strip()
    return expected


def assert_result(test, result, fixture):
    status, output, error = result if isinstance(result, tuple) else (
        result.returncode, result.stdout, result.stderr)
    expected = fixture_expectations(fixture)
    test.assertEqual(status, expected["status"], error)
    test.assertEqual(output, expected["stdout"])
    if expected["stderr_pattern"]:
        test.assertRegex(error, expected["stderr_pattern"])
    elif status in (2, 3):
        test.assertTrue(error.strip(), "Expected an error or UNKNOWN explanation")
    else:
        test.assertEqual(error, "")
