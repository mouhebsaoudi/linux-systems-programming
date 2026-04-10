#!/usr/bin/env python3

import sys, os, tempfile, random
from pathlib import Path

from testsupport import run, run_project_executable, subtest
from fuse_helpers import run_background, fuse_unmount, fuse_mount, gen_mnt_path, fuse_check_mnt

def main() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        temp_path = Path(tmpdir)
        mnt_path  = fuse_mount(temp_path, "memfs_mnt")

        fuse_check_mnt(tmpdir, mnt_path)

        with subtest("Check that filesystem handles symlinks correctly"):
            source = "foo"
            sourcepath = os.path.join(mnt_path, source)

            dest = "bar"
            destpath = os.path.join(mnt_path, dest)

            os.symlink(sourcepath, destpath)

            if not os.path.islink(destpath):
                print(f"{destpath} is not a symbolic link")
                fuse_unmount(mnt_path)
                exit(1)

            num_bytes = random.randint(1, 512)

            with open(sourcepath, "wb") as f:
                bytes_src = os.urandom(num_bytes)
                f.write(bytes_src)

            with open(destpath, "rb") as f:
                bytes_dst = f.read()

            if len(bytes_src) != len(bytes_dst) or bytes_src != bytes_dst:
                print("Filesystem returned wrong data when reading from symlink")
                fuse_unmount(mnt_path)
                exit(1)

            fuse_unmount(mnt_path)
            sys.exit(0)

if __name__ == "__main__":
    main()