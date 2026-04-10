#!/usr/bin/env python3

import os
import socket
import subprocess
import tempfile

from testsupport import info, run_project_executable, warn, subtest
from processtest_helpers import ensure_dependencies


def main() -> None:
    ensure_dependencies()
    hostname = socket.gethostname()

    with subtest("Test running single command 'echo hostname'"):
        proc = run_project_executable(
            "shell", input=f"echo {hostname}\n", stdout=subprocess.PIPE
        )
        out = proc.stdout.strip()
        assert out == hostname, f"shell stdout: expected: '{hostname}', got '{out}'"
        info("OK")

    with subtest("Test running single command that does not exist"):
        proc = run_project_executable(
            "shell", input=f"dev_null_foo\n", stdout=subprocess.PIPE,
            check=False
        )
        assert proc.returncode != 0, f"expected shell error on non-existent executable, got '{proc.returncode}'"
        info("OK")

    with subtest("Test running single command with invalid arguments"):
        proc = run_project_executable(
            "shell", input=f"ls -P\n", stdout=subprocess.PIPE, check=False
        )
        assert proc.returncode != 0, f"expected shell error on invalid options passed to executable"
        info("OK")

    with subtest("Test number of file descriptor with 'ls /proc/self/fd'"):
        proc = run_project_executable(
            "shell",
            input="ls /proc/self/fd\n",
            stdout=subprocess.PIPE,
            extra_env=dict(LC_ALL="C"),
        )
        out = proc.stdout.strip()
        expected = "0\n1\n2\n3"
        assert (
            out == expected
        ), f"Child process has not expected number of file descriptors: expected: '{expected}', got '{out}'"
        info("OK")


if __name__ == "__main__":
    main()
