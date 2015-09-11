#!/bin/sh

mkdir -p baseline && cd baseline

cat << EOF > baseline.py
#!/usr/bin/env python
# encoding: utf-8

import os
import sys
import pprint
import hashlib

# This is the most simple dupefinder in python that does not crash on large
# datasets.  It is inefficient and ugly, but works as minimal comparison tool.


BLOCKSIZE = 6 * (1024 ** 2)


def hash_file(file_path):
    try:
        with open(file_path, 'rb') as fd:
            m = hashlib.md5()
            buf = fd.read(BLOCKSIZE)

            while len(buf) > 0:
                m.update(buf)
                buf = fd.read(BLOCKSIZE)

            return m.hexdigest()
    except OSError:
        return


def find_dups(input_dir):
    hashes, dups = {}, {}
    for path, dirs, files in os.walk(input_dir):
        abspathes = (os.path.join(path, n) for n in files)
        for file_path in filter(os.path.isfile, abspathes):
            md5 = hash_file(file_path)
            if md5 and hashes.setdefault(md5, file_path) is not file_path:
                at = dups.setdefault(md5, [hashes[md5]])
                at.append(file_path)
    return dups


if __name__ == '__main__':
    pprint.pprint(find_dups(sys.argv[1]))
EOF

chmod +x baseline.py
