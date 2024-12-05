import pytest
from tests.utils import cleanup_testdir, create_testdir

@pytest.fixture(autouse=True)
def with_cleanup_between_runs():
    cleanup_testdir()
    create_testdir()
