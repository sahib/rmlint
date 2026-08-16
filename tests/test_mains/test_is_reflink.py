import os
import subprocess

import pytest

from tests.utils import (
    assert_exit_code,
    create_dirs,
    create_file,
    create_link,
    get_testdir,
    has_feature,
    run_rmlint,
    run_rmlint_once,
)

if not has_feature('fiemap'):
    pytest.skip("rmlint was compiled without fiemap support",
                allow_module_level=True)


def check_is_reflink_status(status_code, *paths):
    with assert_exit_code(status_code):
        run_rmlint_once(
            '--is-reflink', *paths,
            use_default_dir=False,
            with_json=False,
            verbosity=''
        )


def test_bad_arguments():
    path_a = create_file('xxx', 'a')
    path_b = create_file('xxx', 'b')
    path_c = create_file('xxx', 'c')
    for paths in [
        (path_a,),
        (path_a, path_b, path_c),
        (path_a, path_a + '.nonexistent')
    ]:
        check_is_reflink_status(1, *paths)  # RM_LINK_NONE


def test_directories():
    path_a = create_dirs('dir_a')
    path_b = create_dirs('dir_b')
    check_is_reflink_status(3, path_a, path_b)  # RM_LINK_NOT_FILE


def test_different_sizes():
    path_a = create_file('xxx', 'a')
    path_b = create_file('xxxx', 'b')
    check_is_reflink_status(4, path_a, path_b)  # RM_LINK_WRONG_SIZE


def test_same_path():
    path_a = create_file('xxx', 'a')
    check_is_reflink_status(6, path_a, path_a)  # RM_LINK_SAME_FILE


def test_path_double():
    path_a = create_file('xxx', 'dir/a')
    create_link('dir', 'dir_symlink', symlink=True)
    path_b = os.path.join(get_testdir(), 'dir_symlink/a')
    check_is_reflink_status(7, path_a, path_b)  # RM_LINK_PATH_DOUBLE


def test_hardlinks():
    path_a = create_file('xxx', 'a')
    path_b = path_a + '_hardlink'
    create_link('a', 'a_hardlink', symlink=False)
    check_is_reflink_status(8, path_a, path_b)  # RM_LINK_HARDLINK


def test_symlink():
    path_a = create_file('xxx', 'a')
    path_b = create_file('xxx', 'b') + '_symlink'
    create_link('b', 'b_symlink', symlink=True)
    check_is_reflink_status(9, path_a, path_b)  # RM_LINK_SYMLINK


def _run_dd_urandom(outfile, blocksize, count, extra=''):
    fmt = 'dd status=none oflag=sync bs={bs} count={c} {e} if=/dev/urandom'
    subprocess.run(
        [*fmt.format(bs=blocksize, c=count, e=extra).split(), 'of=' + outfile],
        check=True,
    )


def _make_reflink_testcase(extents, hole_extents=None, break_link=False):
    path_a = os.path.join(get_testdir(), 'a')
    path_b = os.path.join(get_testdir(), 'b')

    _run_dd_urandom(path_a, '4K', extents)

    if hole_extents is not None:
        os.truncate(path_a, (extents + hole_extents) * 4 * 1024)

    subprocess.run(['cp', '--reflink', path_a, path_b], check=True)

    if break_link:
        _run_dd_urandom(path_b, '4K', 1, 'seek=1 conv=notrunc')

    # expect RM_LINK_NONE or RM_LINK_REFLINK
    check_is_reflink_status(1 if break_link else 0, path_a, path_b)


# GitHub issue #527: Make sure rmlint does not skip every other extent.
@pytest.mark.usefixtures("needs_reflink_fs")
def test_second_extent_differs():
    _make_reflink_testcase(extents=5, break_link=True)


# GitHub issue #528, part 1: Make sure the last extent is not ignored.
@pytest.mark.usefixtures("needs_reflink_fs")
def test_last_extent_differs():
    _make_reflink_testcase(extents=2, break_link=True)


# GitHub issue #528, part 2: Make sure files that end in a hole can be identified as reflinked.
@pytest.mark.usefixtures("needs_reflink_fs")
def test_reflink_ends_with_hole():
    _make_reflink_testcase(extents=1, hole_extents=1)


def _copy_file_range(src, dst, count, offset_src, offset_dst):
    bytes_copied = os.copy_file_range(src, dst, count, offset_src, offset_dst)
    if bytes_copied < count:
        raise RuntimeError(f'copy_file_range only copied {bytes_copied} bytes (expected {count})')


def kb(n):
    return 1024 * n


def _hole_testcase_inner(extents):
    path_a = os.path.join(get_testdir(), 'a')
    path_b = os.path.join(get_testdir(), 'b')

    _run_dd_urandom(path_a, kb(16) // extents, extents)
    with open(path_b, 'wb') as f:
        os.truncate(f.fileno(), kb(16))  # same-sized file with no extents

    with open(path_a, 'rb') as fsrc, open(path_b, 'wb') as fdst:
        infd, outfd = fsrc.fileno(), fdst.fileno()
        yield infd, outfd

    # expect RM_LINK_NONE
    check_is_reflink_status(1, path_a, path_b)


# GitHub issue #611: Make sure holes can be detected when the physical offsets and logical
# extent ends are otherwise the same.
@pytest.mark.usefixtures("needs_reflink_fs")
def test_hole_before_extent():
    for infd, outfd in _hole_testcase_inner(extents=2):
        # copy first half of first extent with 4K offset
        _copy_file_range(infd, outfd, kb(4), kb(0), kb(4))
        # copy second extent
        _copy_file_range(infd, outfd, kb(8), kb(8), kb(8))


# GitHub issue #530: Make sure physically adjacent extents aren't merged if there is a
# hole between them logically.
@pytest.mark.usefixtures("needs_reflink_fs")
def test_hole_between_extents():
    for infd, outfd in _hole_testcase_inner(extents=1):
        # copy first extent
        _copy_file_range(infd, outfd, kb(8), kb(0), kb(0))
        # copy first half of second extent with 4K offset
        _copy_file_range(infd, outfd, kb(4), kb(8), kb(12))


def test_default_outputs_disabled():
    create_file('xxx', 'a')
    create_file('xxx', 'b')

    try:
        run_rmlint('--is-reflink a b', use_default_dir=False, with_json=False)
    except subprocess.CalledProcessError:
        pass  # nonzero exit status is expected

    # --is-reflink should not create or overwrite rmlint.sh or rmlint.json
    assert not os.path.exists(os.path.join(get_testdir(), 'rmlint.sh'))
    assert not os.path.exists(os.path.join(get_testdir(), 'rmlint.json'))
