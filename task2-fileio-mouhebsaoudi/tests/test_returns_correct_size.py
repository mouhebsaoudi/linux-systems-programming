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

        with subtest("Check that filesystem returns correct filesize"):
            filename = "foo"
            filepath = os.path.join(mnt_path, filename)

            num_bytes = random.randint(1, 512)
            with open(filepath, "wb") as f:
                bytes = os.urandom(num_bytes)
                f.write(bytes)

            real_size = os.stat(filepath).st_size

            if real_size != num_bytes:
                print("Filesystem returned the wrong size.")
                fuse_unmount(mnt_path)
                exit(1)

            fuse_unmount(mnt_path)
            sys.exit(0)

if __name__ == "__main__":
    main()