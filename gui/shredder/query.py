#!/usr/bin/env python
# encoding: utf-8

"""Implement a very small query language useful for filtering.

Beside simple string matching the search should be able to
select files with a certain size or in a certain size range.
This is a valid example for example:

    Hello world size:2K-3M count:2,4-10 mtime:today
"""

# Stdlib:
import re
import logging
from collections import defaultdict

try:
    from parsedatetime import Calendar
    HAS_PARSEDATETIME = True
except ImportError:
    HAS_PARSEDATETIME = False


LOGGER = logging.getLogger('query')


def check_numeric(matches, value):
    """Check if numeric `value` matches one of
    the values (single lis) and ranges (two-item list) in `matches`
    """
    if not matches:
        return True

    for match in matches:
        if len(match) is 1 and match[0] == value:
            return True
        elif len(match) is 2:
            vmin, vmax = match
            if vmin <= value <= vmax:
                return True
        elif len(match) > 2:
            LOGGER.warning('Too long match: %s', match)

    return False


class Query:
    """Query is able to parse and evaluate a small query language.

    It should not be directly instantiated.
    """
    def __init__(self):
        """You should not use this."""
        self.name = self.sizes = self.mtimes = self.amounts = None

    @staticmethod
    def parse(query_input):
        """Parse a query and return a fresh Query object."""
        result = parse(query_input)

        qry = Query()
        qry.name = result['name']
        qry.sizes = result['size']
        qry.mtimes = result['mtime']
        qry.amounts = result['count']
        return qry

    def issubset(self, other_query):
        """Check if this query will yield a subset of other_query"""
        if other_query is None or not other_query.name:
            return False

        if self.sizes or self.mtimes or self.amounts:
            # Do not even bother checking:
            # This is gonna go error prone with little effect.
            return False

        # Check if previous query is a prefix to this one.
        return self.name.startswith(other_query.name)

    def matches(self, leaf_node, size, mtime, count):
        """Check if a node matches to this query. Returns True on match"""
        for node in leaf_node.up():
            if self.name in node.name.lower():
                break
        else:
            return False

        if not check_numeric(self.sizes, size):
            return False

        if not check_numeric(self.mtimes, mtime):
            return False

        if not check_numeric(self.amounts, count):
            return False

        return True


def parse_generic_range(value, converter):
    """Parse number collections in the form N[-N[,N[-N]]]...

    For the actual conversion, `converter` will be called.
    It may raise ValueError on invalid input.
    """
    parts = value.split(',')
    results = []

    for part in parts:
        sub_results = []
        for sub in part.split('-', maxsplit=1):
            try:
                parsed = converter(sub)
            except ValueError as err:
                LOGGER.warning('Could not convert value: %s', str(err))
            else:
                if parsed:
                    sub_results.append(parsed)
        results.append(sub_results)

    return results


EXPONENTS = {
    'B': 0,
    'K': 1,
    'M': 2,
    'G': 3,
    'T': 4,
    'P': 5
}


def parse_size_single(value):
    """Convert a size description to a byte amount."""
    value = value.upper()

    for suffix, exponent in EXPONENTS.items():
        if value.endswith(suffix):
            value = value[:-len(suffix)]
            break
    else:
        exponent = 0

    return int(value) * (1024 ** exponent)


def parse_mtime_single(value):
    """Convert a human readable time description to a """
    if not HAS_PARSEDATETIME:
        return int(value)

    calendar = Calendar()
    guess, rc = calendar.parseDT(value)

    if rc is 0:
        LOGGER.warning('Could not parse date: %s', value)
        return int(value)

    return guess.timestamp()


def parse_size(value):
    """Parse size values and ranges."""
    return parse_generic_range(value, parse_size_single)


def parse_mtime(value):
    """Parse mtime values and time ranges."""
    return parse_generic_range(value, parse_mtime_single)


def parse_count(value):
    """Parse count values and ranges."""
    return parse_generic_range(value, int)


VALID_ATTRS = {
    'size': parse_size,
    'mtime': parse_mtime,
    'count': parse_count
}

ATTR_PATTERN = re.compile(
    r'({attrs}):(.*?)(?=\s|$)'.format(
        attrs='|'.join(VALID_ATTRS.keys())
    )
)


def parse(query):
    """Actual lowlevel parsing function.
    Extracts arbitary text and attr-value pairs.
    """
    attrs = ATTR_PATTERN.finditer(query)
    results = defaultdict(list)
    indices = [0]

    for match in attrs:
        attr, value = match.groups()
        indices.extend(match.span())

        if not value:
            continue

        parser = VALID_ATTRS.get(attr)
        if parser is None:
            LOGGER.warning('Invalid parser: %s', parser)
            continue

        parsed_value = parser(value)
        if parsed_value:
            results[attr].extend(parsed_value)

    indices.append(-1)

    # Extract the rest of the text (for string matching)
    parts = []
    query += ' '

    for cnt, idx in enumerate(indices[::2]):
        part = query[idx:indices[2 * cnt+1]].strip()
        if part:
            parts.append(part)

    results['name'] = ' '.join(parts).strip().lower()
    return results


###############
# DEBUG MAIN  #
###############

if __name__ == '__main__':
    import sys

    print('`{}`'.format(parse(sys.argv[1])))
