from tests.utils import *


def test_color(usual_setup_usual_teardown):
    # color disabled by pipe
    output = run_rmlint('-o summary', with_json=False, directly_return_output=True)
    assert b'\x1b[' not in output

    # color cannot be enabled with pipe
    output = run_rmlint('-o summary --with-color', with_json=False, directly_return_output=True)
    assert b'\x1b[' not in output


def test_no_color_crash(usual_setup_usual_teardown):
    # cause rmlint to fail with a simple error message, without color
    result, _ = run_rmlint_once('-kK --no-with-color', with_json=False, check=False, verbosity='')
    assert result.returncode == 1  # should not be a segfault or any other fatal signal
    assert result.stderr
    assert b'\x1b[' not in result.stderr
