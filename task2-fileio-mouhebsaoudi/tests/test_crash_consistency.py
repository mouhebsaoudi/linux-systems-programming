#!/usr/bin/env python3

import os
import sys
import tempfile
from pathlib import Path

from fuse_helpers import fuse_unmount, fuse_mount, fuse_check_mnt, crash_fuse_app
from testsupport import subtest


def main() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        temp_path = Path(tmpdir)
        mnt_path_or = fuse_mount(temp_path, "memfs_mnt_or")

        fuse_check_mnt(tmpdir, mnt_path_or)

        with subtest("Check crash consistency of fuse filesystem"):
            dir_list = [
                    "foo", 
                    os.path.join("foo", "bar"),
            ]
            for dir in dir_list:
                os.mkdir(os.path.join(mnt_path_or, dir))

            file_list = [
                "hello",
                os.path.join("foo", "hello_one"),
                os.path.join("", *["foo", "bar", "hello_two"])
            ]
            for file in file_list:
                f = open(os.path.join(mnt_path_or, file), "w")
                f.write("test")
                f.close()
            
            crash_fuse_app(mnt_path_or)
            mnt_path_new = fuse_mount(temp_path, "memfs_mnt_new")
            fuse_check_mnt(tmpdir, mnt_path_new)

            flat_file_list = ["hello"]
            check_dir_list = os.listdir(mnt_path_new)
            for file in flat_file_list:
                if file not in check_dir_list:
                    fuse_unmount(mnt_path_new)
                    sys.exit(1)
                        
            hier_file_list_level_one = ["hello_one"]
            check_dir_list = os.listdir(os.path.join("", *[mnt_path_new, "foo"]))
            for file in hier_file_list_level_one:
                if file not in check_dir_list:
                    fuse_unmount(mnt_path_new)
                    sys.exit(1)
            
            hier_file_list_level_two = ["hello_two"]
            check_dir_list = os.listdir(os.path.join("", *[mnt_path_new, "foo", "bar"]))
            for file in hier_file_list_level_two:
                if file not in check_dir_list:
                    fuse_unmount(mnt_path_new)
                    sys.exit(1)

            f = open(os.path.join(mnt_path_new, *["foo", "bar", "hello_two"]))
            content = f.read(5)
            f.close()
            if content != "test":
                print("Restored file's data don't match")
                fuse_unmount(mnt_path_new)
                sys.exit(1)

        fuse_unmount(mnt_path_new)
        sys.exit(0)

if __name__ == "__main__":
    main()
