#!/usr/bin/env python3

import os
import subprocess
import tempfile

from testsupport import info, run_project_executable, warn, subtest
from processtest_helpers import ensure_dependencies

def main() -> None:
    ensure_dependencies()

    expected_stderr = os.readlink("/proc/self/fd/2")
    with subtest(
        "Test open file descriptor 'readlink /proc/self/fd/0 /proc/self/fd/1 /proc/self/fd/2 /proc/self/fd/3'"
    ):
        proc = run_project_executable(
            "shell",
            input="readlink /proc/self/fd/0 /proc/self/fd/1 /proc/self/fd/2 /proc/self/fd/3\n",
            stdout=subprocess.PIPE, check=False,
        )
        out = proc.stdout
        lines = out.strip().split("\n")
        assert (
            len(lines) == 3
        ), f"Expected 3 lines in the readlink output, got:\n'{out}'"
        stdin = lines[0]
        assert stdin.startswith(
            "pipe:"
        ), f"Expected stdin (fd=0) to be a pipe, got: '{stdin}'"
        stdout = lines[1]
        assert stdout.startswith(
            "pipe:"
        ), f"Expected stdout (fd=1) to be a pipe, got: '{stdout}'"
        stderr = lines[2]
        assert (
            stderr == expected_stderr
        ), f"Expected stderr (fd=2): '{expected_stderr}', got: '{stderr}'"
        info("OK")


    # Test input and output redirection
    with tempfile.TemporaryDirectory() as dir:
        dest = f"{dir}/file"
        with subtest(
            f"Test shell redirection with 'readlink /proc/self/fd/1 /proc/self/fd/0 < /dev/null {dest}'"
        ):
            proc = run_project_executable(
                "shell",
                input=f"readlink /proc/self/fd/1 /proc/self/fd/0 < /dev/null > {dest}\n",
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
                content == f"{dest}\n/dev/null"
            ), f"Expected {dest} created by shell redirection to contain {dest}\n/dev/null but got: '{content}'"
            info("OK")


    with subtest(f"Test killing both processes in a pipeline cat - | wc"):
        cat_proc = subprocess.Popen(["cat"],
                                    stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE,
                                    text=True)
        wc_proc  = subprocess.Popen(["wc"], stdin=cat_proc.stdout,
                                    stdout=subprocess.PIPE, preexec_fn=os.setsid)
        cmd = f"kill {cat_proc.pid}\nkill {wc_proc.pid}\n"
        proc = run_project_executable("shell", input=cmd, check=False, stdout=subprocess.PIPE)
        wc_finished = wc_proc.wait(timeout=3)
        cat_finished = cat_proc.wait(timeout=3)
        expected_signal = -15
        assert (
            cat_finished == expected_signal and (wc_finished == expected_signal or wc_finished == 0)
        ), f"expected kill to terminate group process with SIGTERM (-15), got: {cat_finished} {wc_finished}"
        info("OK")

        # Test kill with invalid PID
        cmd = f'kill hello\n'
        proc = run_project_executable("shell", input=cmd,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE,
                                      check=False)
        assert (
            proc.returncode != 0
        ), f"expected shell to raise error on kill with invalid PID"
        info("OK")

if __name__ == "__main__":
    main()

