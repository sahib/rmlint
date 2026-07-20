import pytest

from tests import utils


@pytest.fixture(params=["sh", "bash", "dash"])
def shell(request):
    yield request.param


@pytest.fixture
def no_setup_teardown():
    pass


@pytest.fixture(autouse=True)
def usual_setup_usual_teardown(request):
    if "no_setup_teardown"  in request.fixturenames:
        yield
        return
    utils.usual_setup_func()
    yield
    utils.usual_teardown_func()


@pytest.fixture
def usual_setup_mount_bind_teardown(no_setup_teardown):
    utils.usual_setup_func()
    yield
    utils.mount_bind_teardown_func()


@pytest.fixture(scope="session")
def needs_reflink_fs():
    """fixture for tests dependent on reflink-capable testdir"""
    if skip_msg := utils.check_reflink_capable():
        pytest.skip(skip_msg)


@pytest.fixture(scope="session")
def needs_xattr_fs():
    '''TODO'''
