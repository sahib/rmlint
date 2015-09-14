#!/bin/sh

mkdir -p baseline && cd baseline

cat << EOF > baseline.py
#!/usr/bin/env python
# encoding: utf-8
import os
import sys
import json
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


hashes, dups = {}, {}


def find_dups(input_dir):
    cnt = 0

    for path, dirs, files in os.walk(input_dir):
        abspathes = (os.path.join(path, n) for n in files)
        for file_path in filter(os.path.isfile, abspathes):
            # Filter empty files:
            if os.path.getsize(file_path) is 0:
                continue

            md5 = hash_file(file_path)
            if md5 and hashes.setdefault(md5, file_path) != file_path:
                at = dups.setdefault(md5, [hashes[md5]])
                at.append(file_path)
                cnt += 1

    return dups, cnt


if __name__ == '__main__':
    sum_dupes = 0
    for input_dir in sys.argv[1:]:
        dups, cnt = find_dups(input_dir)
        sum_dupes += cnt

    print(json.dumps({
        'dupes': sum_dupes,
        'sets': len(dups)
    }))
EOF

chmod +x baseline.py
