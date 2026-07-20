from tests.utils import TESTDIR_NAME, create_dirs, create_file, create_link, run_rmlint, use_valgrind


def test_small_diffs():
    def create_data(length, flips=()):
        data = ['0'] * length
        for flip in flips:
            data[flip] = '1'
        return ''.join(data)

    if use_valgrind():
        size = 32
    else:
        # Takes horribly long elsewhise
        size = 128

    create_file(create_data(size), 'a')
    create_file(create_data(size, flips=(-1,)), 'b')
    _, *data, _ = run_rmlint('-S a')

    assert len(data) == 0

    create_file(create_data(size, flips=(+1,)), 'a')
    create_file(create_data(size, flips=(-1,)), 'b')
    _, *data, _ = run_rmlint('-S a')

    assert len(data) == 0

    create_file(create_data(size, flips=(+1,)), 'a')
    create_file(create_data(size, flips=(+1,)), 'b')
    _, *data, _ = run_rmlint('-S a')

    assert len(data) == 2

    for i in range(0, size // 2):
        create_file(create_data(size, flips=(+i,)), 'a')
        create_file(create_data(size, flips=(-i,)), 'b')
        _, *data, _ = run_rmlint('-S a')

        assert len(data) == (2 if i in (size - i, 0) else 0)


def test_one_byte_file_negative():
    create_file('1', 'one')
    create_file('2', 'two')
    _, *data, _ = run_rmlint('-S a')

    assert len(data) == 0


def test_one_byte_file_positive():
    create_file('1', 'one')
    create_file('1', 'two')
    _, *data, _ = run_rmlint('-S a')

    assert len(data) == 2


def test_two_hardlinks():
    create_file('xxx', 'a')
    create_link('a', 'b')
    _, *data, footer = run_rmlint('-S a')

    assert len(data) == 2
    assert footer['total_lint_size'] == 0


def test_two_external_hardlinks():
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    create_dirs('sub')
    create_link('a', 'sub/a')
    create_link('a', 'sub/b')
    _, *data, footer = run_rmlint('-S a')

    assert len(data) == 4
    assert footer['total_lint_size'] == 3

    _, *data, footer = run_rmlint(
        f"{TESTDIR_NAME}/sub",
        use_default_dir=False
    )

    # No effective lint: Removing any link will not save any disk space.
    assert len(data) == 2
    assert footer['total_lint_size'] == 0
