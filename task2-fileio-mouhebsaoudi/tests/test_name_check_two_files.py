#!/usr/bin/env python3

import sys
import os
import tempfile
from pathlib import Path

from testsupport import subtest
from fuse_helpers import fuse_unmount, fuse_mount, fuse_check_mnt


def main() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        temp_path = Path(tmpdir)
        mnt_path = fuse_mount(temp_path, "memfs_mnt")

        fuse_check_mnt(tmpdir, mnt_path)

        with subtest("Check that 2 files cannot have same name in same dir"):
            os.mknod(os.path.join(mnt_path, "file"))

            try:
                os.mknod(os.path.join(mnt_path, "file"))
                fuse_unmount(mnt_path)
                sys.exit(1)
            except FileExistsError:
                fuse_unmount(mnt_path)
                sys.exit(0)


if __name__ == "__main__":
    main()
