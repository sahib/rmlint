#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_keep_hardlinks():
    create_file('xxx', 'file_a')
    create_link('file_a', 'file_b')
    create_file('xxx', 'file_z')

    head, *data, footer = run_rmlint('--no-hardlinked -S a')
    assert data[0]["path"].endswith("file_a")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("file_z")
    assert data[1]["is_original"] is False

    head, *data, footer = run_rmlint('--hardlinked -S a')
    assert data[0]["path"].endswith("file_a")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("file_b")
    assert data[1]["is_original"] is False
    assert data[2]["path"].endswith("file_z")
    assert data[2]["is_original"] is False

    head, *data, footer = run_rmlint('--keep-hardlinked -S a')
    assert data[0]["path"].endswith("file_b")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("file_a")
    assert data[1]["is_original"] is True
    assert data[2]["path"].endswith("file_z")
    assert data[2]["is_original"] is False

    head, *data, footer = run_rmlint('--keep-hardlinked -S A')
    assert data[0]["path"].endswith("file_z")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("file_b")
    assert data[1]["is_original"] is False
    assert data[2]["path"].endswith("file_a")
    assert data[2]["is_original"] is False
