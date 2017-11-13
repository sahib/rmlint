#!/usr/bin/env python3
# encoding: utf-8

from nose import with_setup
from tests.utils import *
from nose.plugins.attrib import attr

INCREMENTS = [4096, 1024, 1, 20000]

def streaming_compliance_check(*patterns):
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

@with_setup(usual_setup_func, usual_teardown_func)
def test_murmur():
    streaming_compliance_check('murmur')

@with_setup(usual_setup_func, usual_teardown_func)
def test_metro():
    streaming_compliance_check('metro')

@with_setup(usual_setup_func, usual_teardown_func)
def test_glib():
    streaming_compliance_check('md5', 'sha1', 'sha256', 'sha512')

@with_setup(usual_setup_func, usual_teardown_func)
def test_sha3():
    streaming_compliance_check('sha3')

@with_setup(usual_setup_func, usual_teardown_func)
def test_blake():
    streaming_compliance_check('blake')

@with_setup(usual_setup_func, usual_teardown_func)
def test_xx():
    streaming_compliance_check('xxhash')

@with_setup(usual_setup_func, usual_teardown_func)
def test_highway():
    streaming_compliance_check('highway')

@attr("known_issue")
@with_setup(usual_setup_func, usual_teardown_func)
def test_cumulative():
    streaming_compliance_check('cumulative')

