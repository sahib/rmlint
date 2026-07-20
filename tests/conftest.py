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


def skip_if_no_reflink_fs():
    if err_msg := utils.check_reflink_capable():
        pytest.skip(err_msg)


@pytest.fixture(scope="session")
def needs_reflink_fs():
    """fixture for tests dependent on reflink-capable testdir"""
    if skip_msg := utils.check_reflink_capable():
        pytest.skip(skip_msg)
