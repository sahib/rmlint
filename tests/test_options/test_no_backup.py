#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *

import os
import re
import shutil
import tempfile


@with_setup(usual_setup_func, usual_teardown_func)
def test_backup():
    create_file('content', 'name_x')
    create_file('content', 'name_y')

    temp_dir = tempfile.mkdtemp()
    try:
        test_path = os.path.join(temp_dir, "xxx.json")

        for _ in range(10):
            _, *data, _ = run_rmlint("-o json:" + test_path, outputs=[])
            assert len(data) == 2

        for path in os.listdir(temp_dir):
            assert path.startswith("xxx.")

            if path == "xxx.json":
                continue

            assert re.match(
                r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(|\.\d+)Z',
                path[len("xxx."):-len(".json")]
            ) is not None
    finally:
        shutil.rmtree(temp_dir)
