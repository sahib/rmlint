import os
import shutil

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
    if utils.get_env_flag('print_cmd') and config.option.log_cli_level is None:
        config.option.log_cli_level = "INFO"
        config.option.log_cli_format = "%(message)s"

    if not os.getenv('RM_TS_DIR') or config.option.basetemp is not None:
        return

    if not os.path.isdir(utils.TESTDIR_BASE):
        raise pytest.UsageError(
            f"RM_TS_DIR={utils.TESTDIR_BASE} does not exist. "
            "It is expected to be provisioned on a suitable filesystem."
        )

    # a subdirectory, never RM_TS_DIR itself: pytest remove basetemp
    # before recreating it, which fails on a mountpoint.
    basetemp = os.path.join(utils.TESTDIR_BASE, 'pytest')

    # test if there are leftovers of a root invocation
    if not os.access(utils.TESTDIR_BASE, os.W_OK | os.X_OK):
        raise pytest.UsageError(
            f"RM_TS_DIR={utils.TESTDIR_BASE} is not writable."
        )

    if os.path.exists(basetemp) and not os.access(basetemp, os.W_OK | os.X_OK):
        raise pytest.UsageError(
            f"{basetemp} exists but cannot be removed. "
            "Possibly left behind by a previous sudo pytest."
        )

    config.option.basetemp = basetemp


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
    if utils.get_env_flag('always_clean'):
        shutil.rmtree(tmp_path, ignore_errors=True)


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
