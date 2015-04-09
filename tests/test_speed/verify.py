import sys
import json
import hashlib

from collections import defaultdict


def sha1_of_file(path):
    sha = hashlib.sha1()
    with open(path, 'rb') as f:
        while True:
            block = f.read(2 ** 10)
            if not block:
                break

            sha.update(block)
        return sha.hexdigest()



if __name__ == '__main__':
    head, *data, footer = json.load(sys.stdin)

    dupe_groups = defaultdict(list)
    for point in data:
        dupe_groups[point['checksum']].append(point['path'])

    for checksum, group in dupe_groups.items():
        checksum_set = set()
        for path in group:
            sha1 = sha1_of_file(path)
            checksum_set.add(sha1)
            if len(checksum_set) > 1:
                print('The following path differs:')
                print('    {c}={p}'.format(p=path, c=sha1))
                print('It was in a group with this checksum')

                checksum_set.remove(sha1)
                print('    {c}={p}'.format(p=group, c=checksum_set.pop()))
                sys.exit(-1)

    print('Everything fine.')
