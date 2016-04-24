#!/usr/bin/env python3
# encoding: utf-8
def teardown():
    from .utils import usual_teardown_func

    try:
        usual_teardown_func()
    except OSError:
        pass


def setup():
    from .utils import usual_setup_func
    usual_setup_func()
