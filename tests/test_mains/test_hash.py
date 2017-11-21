#!/usr/bin/env python3
# encoding: utf-8

from nose import with_setup
from tests.utils import *
from nose.plugins.attrib import attr
from parameterized import parameterized

INCREMENTS = [4096, 1024, 1, 20000]

def streaming_compliance_check(patterns):
    # a valid hash function streaming function should satisfy hash('a', 'b', 'c') == hash('abc')

    a = create_file('1' * 10000, 'a')

    algos = []
    for pattern in patterns:
        algos += [algo for algo in CKSUM_TYPES if pattern in algo]

    cmd = './rmlint --hash --increment {increment} --algorithm {algo} {path}'

    for algo in algos:
        command = cmd.format(increment=INCREMENTS[0], algo=algo, path=a)
        output0 = subprocess.check_output(command.split())
        for increment in INCREMENTS[1:]:
            command = cmd.format(increment=increment, algo=algo, path=a)
            output = subprocess.check_output(command.split())
            if(output!=output0):
                assert False, "{} fails streaming test with increment {}".format(algo, increment)
                break

@parameterized([
        'murmur',
        'metro',
        ['glib:', 'md5', 'sha1', 'sha256', 'sha512'],
        'sha3',
        'blake',
        'xxhash',
        'highway'
        ])
def test_hash_function(*pat):
    if(len(pat)==1):
        streaming_compliance_check(pat)
    else:
        streaming_compliance_check(pat[1:])

