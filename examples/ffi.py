"""Run: python3 examples/ffi.py .suka/main.suka [--library path/to/libsuka.so]."""

import argparse
import ctypes
import os
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description="Call the Suka C ABI with ctypes")
    parser.add_argument("file", type=Path)
    parser.add_argument("--library", type=Path,
                        default=Path(__file__).resolve().parents[1] / "build" / "libsuka.so")
    args = parser.parse_args()
    library = ctypes.CDLL(str(args.library.resolve()))
    library.suka_run_file.argtypes = [ctypes.c_char_p]
    library.suka_run_file.restype = ctypes.c_void_p
    library.suka_result_status.argtypes = [ctypes.c_void_p]
    library.suka_result_status.restype = ctypes.c_int
    for name in ("suka_result_output", "suka_result_error"):
        function = getattr(library, name)
        function.argtypes = [ctypes.c_void_p]
        function.restype = ctypes.c_char_p
    library.suka_result_free.argtypes = [ctypes.c_void_p]
    library.suka_result_free.restype = None

    result = library.suka_run_file(os.fsencode(args.file))
    if not result:
        raise MemoryError("could not allocate a Suka result")
    try:
        sys.stdout.write(library.suka_result_output(result).decode("utf-8"))
        sys.stderr.write(library.suka_result_error(result).decode("utf-8"))
        return library.suka_result_status(result)
    finally:
        library.suka_result_free(result)


if __name__ == "__main__":
    raise SystemExit(main())
