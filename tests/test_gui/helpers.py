"""Helpers for the GUI test suite."""
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
GUI_DIR = REPO_ROOT / 'gui'
RESOURCE_DIR = GUI_DIR / 'shredder' / 'resources'
RMLINT_BINARY = REPO_ROOT / 'rmlint'


def import_shredder(fail=False):
    """Import the shredder package or skip the calling test module."""

    if GUI_DIR not in sys.path:
        sys.path.insert(0, str(GUI_DIR))

    try:
        import shredder

    # gi.require_version() raises ValueError
    except (ImportError, ValueError) as err:
        reason = f'failed import: {err}'
        if fail:
            pytest.fail(reason)
        else:
            pytest.skip(reason, allow_module_level=True)

    return shredder
