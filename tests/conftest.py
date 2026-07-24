import os

import pytest

from tests import utils


@pytest.hookimpl(tryfirst=True)
def pytest_configure(config):
    """
    RM_TS_DIR is expected to sit on a filesystem chosen for the suite's needs,
    e.g. a tmpfs with enough space and inodes, a reflink or user-xattr capable
    filesystem, NFS, etc).

    This must run before `_pytest.tmpdir.pytest_configure`.
    hookimpl(tryfirst) should do the work, `_check_basetemp` checks it.

    When running in parallel with xdist, basetemp might be already set.
    """
    if not os.getenv('RM_TS_DIR') or config.option.basetemp is not None:
        return

    if not os.path.isdir(utils.TESTDIR_BASE):
        raise pytest.UsageError(
            f"RM_TS_DIR={utils.TESTDIR_BASE} does not exist. "
            "It is expected to be provisioned on a suitable filesystem."
        )

    # a subdirectory, never RM_TS_DIR itself: pytest remove basetemp
    # before recreating it, which fails on a mountpoint.
    config.option.basetemp = os.path.join(utils.TESTDIR_BASE, 'pytest')


@pytest.fixture(scope="session", autouse=True)
def _check_basetemp(tmp_path_factory):
    """Fail loudly if the tests are not running under RM_TS_DIR after all.
    Also catches a --basetemp pointing off the intended dir.
    """
    if not os.getenv('RM_TS_DIR'):
        return

    basetemp = tmp_path_factory.getbasetemp()
    if utils.TESTDIR_BASE not in map(str, basetemp.parents):
        raise pytest.UsageError(
            f"basetemp {basetemp} is not below RM_TS_DIR {utils.TESTDIR_BASE}"
        )


@pytest.fixture(autouse=True)
def rmlint_testdir(tmp_path):
    """Give each test its own directory, so the suite can run in parallel."""
    utils.set_testdir(str(tmp_path))
    yield tmp_path
    utils.set_testdir(None)


@pytest.fixture(params=["sh", "bash", "dash"])
def shell(request):
    yield request.param


@pytest.fixture(scope="session")
def needs_reflink_fs():
    """fixture for tests dependent on reflink-capable testdir"""
    if skip_msg := utils.check_reflink_capable():
        pytest.skip(skip_msg)


@pytest.fixture(scope="session")
def needs_xattr_fs():
    """fixture for tests dependent on an xattr-capable testdir"""
    if skip_msg := utils.check_xattr_capable():
        pytest.skip(skip_msg)
