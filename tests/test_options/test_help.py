"""
These tests are only here to check if printing help works.
Well, actually it's to increase coverage to be honest.
"""
import subprocess


def test_help():
    yelp = subprocess.check_output(
        ['./rmlint', '--help'], stderr=subprocess.STDOUT
    ).decode('utf-8')
    assert 'man 1 rmlint' in yelp
    assert '--show-man' in yelp


def test_man():
    yelp = subprocess.check_output(
        ['./rmlint', '--show-man'], stderr=subprocess.STDOUT
    ).decode('utf-8')
    assert 'Pahl' in yelp
    assert 'Thomas' in yelp
