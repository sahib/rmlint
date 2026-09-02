import gettext
import locale
import logging
import os
from pathlib import Path

LOGGER = logging.getLogger('i18n')
DOMAIN = 'rmlint'
INSTALL_PREFIX = Path(__file__).resolve().parents[3]
LOCALE_DIR = os.environ.get('TEXTDOMAINDIR',
                            INSTALL_PREFIX / 'share' / 'locale')


def _translation():
    # setlocale() is needed by Gtk usage of libc.
    try:
        locale.setlocale(locale.LC_ALL, '')
    except locale.Error as err:
        LOGGER.debug('Cannot use the locale: %s', err)

    return gettext.translation(DOMAIN, LOCALE_DIR, fallback=True)


_TRANSLATION = _translation()

_ = _TRANSLATION.gettext
ngettext = _TRANSLATION.ngettext
