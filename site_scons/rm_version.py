import re
import subprocess
from pathlib import Path
from typing import NamedTuple

REPO_ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = REPO_ROOT / '.version'

# we accept a subset of SemVer.
VERSION_RE = re.compile(
    r'^(?P<major>0|[1-9]\d*)'
    r'\.(?P<minor>0|[1-9]\d*)'
    r'\.(?P<patch>0|[1-9]\d*)'
    r'(?:-(?P<prerelease>(?:dev|alpha|beta|rc)(?:\.(?:0|[1-9]\d*))?))?$'
)

# We are not a Python-only project, but we use the same solution found in
# https://setuptools-scm.readthedocs.io/latest/usage/#git-archives
GIT_ARCHIVAL_FILE = REPO_ROOT / '.git_archival.txt'

# Python packaging is doing things their way :|
PEP_440 = {'dev': '.dev', 'alpha': 'a', 'beta': 'b', 'rc': 'rc'}

class VersionError(ValueError):
    """Cannot parse .version"""


class Version(NamedTuple):
    major: int
    minor: int
    patch: int
    prerelease: str | None
    name: str

    def __str__(self) -> str:
        """SemVer"""
        return f'{self.release}-{self.prerelease}' if self.prerelease else self.release

    @property
    def release(self) -> str:
        return f'{self.major}.{self.minor}.{self.patch}'

    @property
    def pep440(self) -> str:
        if not self.prerelease:
            return self.release

        kind, _, number = self.prerelease.partition('.')
        return f'{self.release}{PEP_440[kind]}{number or 0}'

    def with_rev(self, git_rev: str) -> str:
        """SemVer with passed git revision as build metadata"""
        if not git_rev:
            return str(self)
        return f'{self}+g{git_rev}'


def read_version(path=VERSION_FILE) -> Version:
    """Parse the .version file."""
    text = Path(path).read_text(encoding='utf-8').strip()
    version, separator, name = text.partition(' ')

    if not separator or not name.strip():
        raise VersionError(f'{path}: expected "<version> <Absurd Codename>", got {text!r}')

    match = VERSION_RE.match(version)
    if match is None:
        raise VersionError(f"{path}: {version!r} is not an accepted version.")

    return Version(
        major=int(match['major']),
        minor=int(match['minor']),
        patch=int(match['patch']),
        prerelease=match['prerelease'],
        name=name.strip(),
    )


def _git(*args):
    try:
        proc = subprocess.run(
            ('git', '-C', str(REPO_ROOT), *args),
            capture_output=True, text=True, check=False,
        )
    except OSError:
        return None
    return proc.stdout.strip() if proc.returncode == 0 else None


GIT_REV_LENGTH = 8
def head_rev():
    top = _git('rev-parse', '--show-toplevel')
    if top and Path(top).resolve() == REPO_ROOT:
        return _git('rev-parse', f'--short={GIT_REV_LENGTH}', 'HEAD')
    return None


def archival_rev():
    try:
        text = GIT_ARCHIVAL_FILE.read_text(encoding='utf-8')
    except OSError:
        return None

    for line in text.splitlines():
        key, _, value = line.partition(':')
        if key.strip() != 'node':
            continue
        value = value.strip()
        return value[:GIT_REV_LENGTH] if not value.startswith('$Format:') else None
    return None
