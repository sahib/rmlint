#!/usr/bin/env python
# encoding: utf-8

"""
General purpose logger, used in many python projects by @sahib.
It has colors and symbols! Enough reasoning.
"""


# Stdlib:
import logging
import logging.handlers


COLORED_FORMAT = "%(asctime)s%(reset)s %(log_color)s[logsymbol] \
%(levelname)-8s%(reset)s \
%(bold_black)s%(name)s:%(lineno)d:%(reset)s %(message)s"

SIMPLE_FORMAT = "%(asctime)s %(levelname)-8s [%(filename)s:%(lineno)3d] \
%(name)s: %(message)s"

DATE_FORMAT = "%H:%M:%S"
UNICODE_ICONS = {
    logging.DEBUG: '⚙',
    logging.INFO: '⚐',
    logging.WARNING: '⚠',
    logging.ERROR: '⚡',
    logging.CRITICAL: '☠'
}


def create_logger(name=None, path=None, verbosity=logging.DEBUG):
    '''Create a new Logger configured with moosecat's defaults.

    :name: A user-define name that describes the logger
    :path: Path to store a log file.
    :return: A new logger .
    '''
    logger = logging.getLogger(name)

    # This is hack to see if this function was already called
    if len(logging.getLogger(None).handlers) == 2:
        return logger

    # Defaultformatter, used for File logging,
    # and for stdout if colorlog is not available
    formatter = logging.Formatter(
        SIMPLE_FORMAT,
        datefmt=DATE_FORMAT
    )

    # Try to load the colored log and use it on success.
    # Else we'll use the SIMPLE_FORMAT
    try:
        import colorlog

        class SymbolFormatter(colorlog.ColoredFormatter):
            """Hack to replace the symbol marker with the actual icon."""
            def format(self, record):
                result = colorlog.ColoredFormatter.format(self, record)
                return result.replace(
                    '[logsymbol]',
                    UNICODE_ICONS[record.levelno]
                )

    except ImportError:
        print('Could not import colorlog. This is not an error.')
        print('You just will not have colors in the logging output...')
        col_formatter = formatter
    else:
        col_formatter = SymbolFormatter(
            COLORED_FORMAT,
            datefmt=DATE_FORMAT,
            reset=False
        )

    # Stdout-Handler
    stream = logging.StreamHandler()
    stream.setFormatter(col_formatter)

    # Rotating File-Handler
    file_stream = logging.handlers.RotatingFileHandler(
        filename=path or '/tmp/app.log',
        maxBytes=(1024 ** 2 * 10),  # 10 MB
        backupCount=2,
        delay=True
    )
    file_stream.setFormatter(formatter)

    # Create the logger and configure it.
    logger.addHandler(file_stream)
    logger.addHandler(stream)
    logger.setLevel(verbosity)
    return logger


if __name__ == '__main__':
    LOGGER = create_logger('Marc')
    LOGGER.setLevel(logging.DEBUG)

    LOGGER.critical('A clitical error.')
    LOGGER.error('Bad, but not clitical.')
    LOGGER.warning('I think stackoverflow is down?!')
    LOGGER.info('Ah, back again.')
    LOGGER.debug('Oh, you can see debug messages too?')
