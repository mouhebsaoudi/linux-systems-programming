#!/usr/bin/env python3

import subprocess
import os
import resource

from testsupport import info, run_project_executable, subtest
from processtest_helpers import ensure_dependencies


pipeLength = 5


def main() -> None:
    ensure_dependencies()

    src_file = os.path.abspath(__file__)
    test_line = "grep-should-find-this"
    with subtest(
        f"Test pipe implementation by running 'cat {src_file} | grep {test_line}'"
    ):
        proc = run_project_executable(
            "shell",
            input=f"cat {src_file} | grep {test_line}\n",
            stdout=subprocess.PIPE,
        )

        out = proc.stdout.strip()
        expected = f'test_line = "{test_line}"'
        assert out == expected, f"expect pipe output to be: {expected}, got '{out}'"
        info("OK")

    src_file = "./tests/test_pipes.py"
    test_line = "testfile"
    with subtest(
        f"Testing input command1 | command2 | ... | commandN by running 'cat {src_file} | grep {test_line}' multiple times"
    ):
        concat = "cat ./tests/testfile1.txt | grep testfile"
        for _ in range(0, pipeLength):
            concat += " | xargs cat | grep ./tests/testfile"
        concat += "\n"
        proc = run_project_executable(
            "shell",
            input=f"{concat}",
            stdout=subprocess.PIPE,
        )

        out = proc.stdout.strip()
        expected = f'./tests/testfile1.txt'
        assert out == expected, f"expect pipe output to be: {expected}, got '{out}'"
        info("OK")


if __name__ == "__main__":
    main()