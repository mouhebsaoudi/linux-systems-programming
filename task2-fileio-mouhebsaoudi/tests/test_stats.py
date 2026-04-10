#!/usr/bin/env python3

import sys, os, tempfile, secrets
from pathlib import Path

from testsupport import run, run_project_executable, subtest
from fuse_helpers import run_background, fuse_unmount, fuse_mount, gen_mnt_path, fuse_check_mnt

def main() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        temp_path = Path(tmpdir)
        mnt_path  = fuse_mount(temp_path, "memfs_mnt")

        fuse_check_mnt(tmpdir, mnt_path)
        data = "Test Stats"
        large = secrets.token_hex(2048)
        with subtest("Check empty stats"):
            fd = os.open(mnt_path,os.O_RDONLY)
            stat = os.fstatvfs(fd)
            os.close(fd)
            if not stat.f_files == 0:
                sys.exit(-1)
            if not stat.f_namemax == 255:
                sys.exit(-1)
            if not stat.f_blocks == 0:
                sys.exit(-1)

        with subtest("Check stats with directory"):
            os.mkdir(os.path.join(mnt_path, "test_dir"))
            fd = os.open(mnt_path,os.O_RDONLY)
            stat = os.fstatvfs(fd)
            os.close(fd)
            if not stat.f_files == 0:
                sys.exit(-1)
            if not stat.f_namemax == 255:
                sys.exit(-1)
            if not stat.f_blocks == 0:
                sys.exit(-1)

        with subtest("Check stats with file"):
            with open(os.path.join(mnt_path, "test_file"), "w") as f:
                f.write(data)
            fd = os.open(mnt_path,os.O_RDONLY)
            stat = os.fstatvfs(fd)
            os.close(fd)
            if not stat.f_files == 1:
                sys.exit(-1)
            if not stat.f_namemax == 255:
                sys.exit(-1)
            if stat.f_blocks < len(data):
                sys.exit(-1)
            if stat.f_blocks > len(data)*1.1:
                sys.exit(-1)

        with subtest("Check stats with multiple files"):
            with open(os.path.join(mnt_path, "test_file2"), "w") as f:
                f.write(large)
            fd = os.open(mnt_path,os.O_RDONLY)
            stat = os.fstatvfs(fd)
            os.close(fd)
            if not stat.f_files == 2:
                sys.exit(-1)
            if not stat.f_namemax == 255:
                sys.exit(-1)
            if stat.f_blocks < (len(data) + len(large)):
                sys.exit(-1)
            if stat.f_blocks > (len(data)*1.1 + len(large)*1.1):
                sys.exit(-1)


        fuse_unmount(mnt_path)
        sys.exit(0)

if __name__ == "__main__":
    main()
