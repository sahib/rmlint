import pytest

from tests.utils import mount_bind_teardown_func, usual_setup_func, usual_teardown_func

@pytest.fixture(params=["sh", "bash", "dash"])
def shell(request):
    yield request.param


@pytest.fixture
def usual_setup_usual_teardown():
    usual_setup_func()
    yield
    usual_teardown_func()


@pytest.fixture
def usual_setup_mount_bind_teardown():
    usual_setup_func()
    yield
    mount_bind_teardown_func()
