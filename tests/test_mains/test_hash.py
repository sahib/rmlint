import subprocess

import pytest

from tests.utils import CKSUM_TYPES, RMLINT_BINARY, create_file

INCREMENTS = [4096, 1024, 1, 20000]

def streaming_compliance_check(patterns):
    # a valid hash function streaming function should satisfy hash('a', 'b', 'c') == hash('abc')

    a = create_file('1' * 10000, 'a')

    algos = []
    for pattern in patterns:
        algos += [algo for algo in CKSUM_TYPES if pattern in algo]

    def run_hash(algo, increment):
        return subprocess.check_output([
            RMLINT_BINARY, '--hash',
            '--increment', str(increment),
            '--algorithm', algo,
            a,
        ])

    for algo in algos:
        output0 = run_hash(algo, INCREMENTS[0])
        for increment in INCREMENTS[1:]:
            if run_hash(algo, increment) != output0:
                assert False, f"{algo} fails streaming test with increment {increment}"


@pytest.mark.parametrize("pat", (
        'murmur',
        'metro',
        ('glib:', 'md5', 'sha1', 'sha256', 'sha512'),
        'sha3',
        'blake',
        'xxhash',
        'highway'
        ))
def test_hash_function(pat):
    streaming_compliance_check((pat,) if isinstance(pat, str) else pat[1:])
