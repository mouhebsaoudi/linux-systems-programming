#!/usr/bin/env python3

import os
import socket
import subprocess
import tempfile

from testsupport import info, run_project_executable, warn, subtest
from processtest_helpers import ensure_dependencies


def main() -> None:
    ensure_dependencies()

    # Test input redirection
    with subtest("Test shell rediretion 'readlink /proc/self/fd/0 < /dev/null'"):
        proc = run_project_executable(
            "shell",
            input="readlink /proc/self/fd/0 < /dev/null\n",
            stdout=subprocess.PIPE,
        )
        out = proc.stdout.strip()
        expected = "/dev/null"
        assert (
            out == expected
        ), f"Child process has not expected number of file descriptors: expected: '{expected}', got '{out}'"
        info("OK")

    # Test input redirection with non-existent file
    with subtest("Test shell rediretion 'readlink /proc/self/fd/0 < /dev/null/file'"):
        proc = run_project_executable(
            "shell",
            input="readlink /proc/self/fd/0 < /dev/null/file\n",
            stdout=subprocess.PIPE,
            check=False
        )
        out = proc.stdout.strip()
        assert (
            proc.returncode != 0
        ), f"Expected shell to return error on redirection from invalid file\n"
        info("OK")

    # Test input redirection with empty file
    with subtest("Test shell rediretion 'echo < /dev/null'"):
        proc = run_project_executable(
            "shell",
            input="echo < /dev/null\n",
            stdout=subprocess.PIPE,
        )
        out = proc.stdout.strip()
        assert (
            out == ""
        ), f"Child process output from stdout should be empty but got '{out}'"
        info("OK")

    # Test output redirection
    with tempfile.TemporaryDirectory() as dir:
        dest = f"{dir}/file"
        with subtest(
            f"Test shell redirection with 'readlink /proc/self/fd/1 > {dir}/file'"
        ):
            proc = run_project_executable(
                "shell",
                input=f"readlink /proc/self/fd/1 > {dest}\n",
                stdout=subprocess.PIPE,
            )
            out = proc.stdout.strip()
            assert (
                out == ""
            ), f"Child process output from stdout should be empty but got '{out}'"
            assert os.path.isfile(
                dest
            ), f"Expected shell redirection to create {dest}, however file does not exists"
            try:
                with open(dest) as f:
                    content = f.read().strip()
            except IOError as e:
                warn(
                    f"Expected shell redirection to create a file with readable permission but got Exception {e} when opening it"
                )
                exit(1)
            assert (
                content == dest
            ), f"Expected {dest} created by shell redirection to contain '{dest}' but got: '{content}'"
            info("OK")

if __name__ == "__main__":
    main()
