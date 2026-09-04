"""Helper to get Shredder/rmlint version"""

def _guess_rmlint_version() -> str:
    """Execute rmlint --version to extract the version.

    Shredder is always versioned the same way as rmlint.
    This is to make version problems less likely.
    """
    from gi.repository import Gio

    proc = Gio.Subprocess.new(
        ['rmlint', '--version'],
        Gio.SubprocessFlags.STDERR_PIPE
    )
    result, _, data = proc.communicate_utf8()
    if result and data:
        import re

        match = re.search(
            r'version (\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?)', data)
        if match:
            return match.group(1)

    return '?.?.?'


def get_version() -> str:
    """Return Shredder version"""
    try:
        from ._version import __version__
    except ImportError:
        return _guess_rmlint_version()

    return __version__
