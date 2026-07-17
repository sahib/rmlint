from tests.utils import create_file, run_rmlint, run_rmlint_pedantic


def test_simple(usual_setup_usual_teardown):
    create_file('1234567890', 'a10')
    create_file('x23456789x', 'b10')

    _, *data, _ = run_rmlint_pedantic('-D')
    assert len(data) == 0

    for suffix in ['-Q .9 -q .1', '-Q 9 -q .1', '-Q .9 -q 1', '-Q 90% -q 10%']:
        _, *data, _ = run_rmlint('-D --rank-by a ' + suffix)
        assert data[0]['path'].endswith('a10')
        assert data[1]['path'].endswith('b10')
        assert len(data) == 2


def test_almost_empty(usual_setup_usual_teardown):
    create_file('x', 'a1')
    create_file('x', 'b1')

    _, *data, footer = run_rmlint('-D -Q .5')
    assert len(data) == 0
    assert footer['total_files'] == 0

    _, *data, footer = run_rmlint('-D -q 1')
    assert len(data) == 0
    assert footer['total_files'] == 0


def test_absolute(usual_setup_usual_teardown):
    data1 = ['x'] * 2048
    data2 = ['x'] * 2048
    data2[1023] = 'y'

    create_file(''.join(data1), 'a')
    create_file(''.join(data2), 'b')

    _, *data, _ = run_rmlint('-D')
    assert len(data) == 0

    _, *data, _ = run_rmlint_pedantic('-D -q 1kb -S a')
    assert data[0]['path'].endswith('a')
    assert data[1]['path'].endswith('b')
    assert len(data) == 2

    _, *data, _ = run_rmlint_pedantic('-D -q 1024 -S a')
    assert data[0]['path'].endswith('a')
    assert data[1]['path'].endswith('b')
    assert len(data) == 2

    _, *data, _ = run_rmlint_pedantic('-D -q 1023 -S a')
    assert len(data) == 0


def test_clamped_to_empty(usual_setup_usual_teardown):
    create_file('x', 'empties/a')
    create_file('x', 'empties/b')

    _, *data, footer = run_rmlint('-q 0 -Q 0')
    assert len(data) == 0
    assert footer['total_files'] == 0

    _, *data, footer = run_rmlint('-q 1 -Q 1')
    assert len(data) == 0
    assert footer['total_files'] == 0

    _, *data, footer = run_rmlint('-q 10 -Q 1')
    assert len(data) == 0
    assert footer['total_files'] == 0

    # Just to check the test actually works normally:
    _, *data, footer = run_rmlint('-q 0 -Q 10')
    assert len(data) == 2
    assert footer['total_files'] == 2
