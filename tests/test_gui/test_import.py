"""Test importing the Python module"""
import os

from tests.test_gui import helpers


def test_import():
    if os.environ.get("DISPLAY"):
        helpers.import_shredder(fail=True)
