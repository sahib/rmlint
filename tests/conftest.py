import pytest

from tests import utils


@pytest.fixture(autouse=True)
def with_cleanup_between_runs():
    utils.cleanup_testdir()
    utils.create_testdir()


@pytest.fixture(params=["sh", "bash", "dash"])
def shell(request):
    yield request.param


@pytest.fixture
def usual_setup_usual_teardown():
    utils.usual_setup_func()
    yield
    utils.usual_teardown_func()


@pytest.fixture
def usual_setup_mount_bind_teardown():
    utils.usual_setup_func()
    yield
    utils.mount_bind_teardown_func()


@pytest.fixture
def needs_reflink_fs():
    """fixture for tests dependent on reflink-capable testdir"""
    if not utils.has_feature('btrfs-support'):
        pytest.skip("btrfs not supported")
    elif not utils.is_on_reflink_fs(utils.TESTDIR_NAME):
        pytest.skip("testdir is not on reflink-capable filesystem")
    yield
