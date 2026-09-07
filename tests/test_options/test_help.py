"""
These tests are only here to check if printing help works.
Well, actually it's to increase coverage to be honest.
"""
import subprocess

import pytest

from tests.utils import RMLINT_BINARY


def test_help():
    yelp = subprocess.check_output(
        (RMLINT_BINARY, '--help'),
        stderr=subprocess.STDOUT,
        text=True
    )
    assert 'man 1 rmlint' in yelp
    assert '--show-man' in yelp


@pytest.mark.manpage
def test_man():
    yelp = subprocess.check_output(
        (RMLINT_BINARY, '--show-man'),
        stderr=subprocess.STDOUT,
        text=True
    )
    assert 'Pahl' in yelp
    assert 'Thomas' in yelp
