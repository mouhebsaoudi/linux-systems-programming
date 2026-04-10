#!/usr/bin/env python3

import os
import sys
import tempfile
from pathlib import Path

from fuse_helpers import fuse_unmount, fuse_mount, fuse_check_mnt
from testsupport import subtest


def main() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        temp_path = Path(tmpdir)
        mnt_path = fuse_mount(temp_path, "memfs_mnt")

        fuse_check_mnt(tmpdir, mnt_path)

        with subtest("Check that 2 directories cannot have same name in same dir"):
            os.mkdir(os.path.join(mnt_path, "dir"))

            try:
                os.mkdir(os.path.join(mnt_path, "dir"))
                fuse_unmount(mnt_path)
                sys.exit(1)
            except FileExistsError:
                fuse_unmount(mnt_path)
                sys.exit(0)


if __name__ == "__main__":
    main()
