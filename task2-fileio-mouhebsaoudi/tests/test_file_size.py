#!/usr/bin/env python3

import os
import sys
import tempfile
import secrets
import time
from pathlib import Path

from fuse_helpers import fuse_unmount, fuse_mount, fuse_check_mnt, crash_fuse_app
from testsupport import subtest


def main() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        temp_path = Path(tmpdir)
        mnt_path = fuse_mount(temp_path, "memfs_mnt")

        fuse_check_mnt(tmpdir, mnt_path)

        file_name = os.path.join(mnt_path, "large")

        with subtest("Create large file"):
            #os.mkdir(os.path.join(mnt_path,"test"))
            f = open(file_name, "w")
            data = secrets.token_hex(1024)
            f.write(data)
            f.close()
            f = open(file_name, "r")
            data_read = f.read()
            f.close()
            if not data == data_read:
                sys.exit(-1)
            fuse_check_mnt(tmpdir,mnt_path)

        with subtest("Large file size correct size"):
            real_size = os.stat(file_name).st_size

            with open(file_name, "r") as f:
                data = f.read()
            if not len(data) == real_size:
                sys.exit(-1)

            with open(file_name, "a") as f:
                data = secrets.token_hex(128)
                f.write(data)

            real_size = os.stat(file_name).st_size
            with open(file_name, "r") as f:
                data = f.read()
            if not len(data) == real_size:
                sys.exit(-1)

        #with subtest("Append to large file"):

        #    os.system(f"ls {mnt_path}")
        #    file_name = os.path.join(mnt_path, "large")
        #    f = open(file_name, "r")
        #    data_read = f.read()
        #    f.close()
        #    data = secrets.token_hex(1024)
        #    with open(file_name, "a") as f:
        #        f.write(data)
        #    correct_data = data_read + data

        #    f = open(file_name, "r")
        #    data_read = f.read()
        #    if not correct_data == data_read:
        #        sys.exit(-1)
        #    fuse_check_mnt(tmpdir,mnt_path)


        #time.sleep(30)
        fuse_unmount(mnt_path)
        sys.exit(0)

if __name__ == "__main__":
    main()
