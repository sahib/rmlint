import csv
import io
import os

from tests.utils import create_file, run_rmlint


def csv_string_to_data(csv_dump):
    return list(csv.DictReader(io.StringIO(csv_dump, newline='')))


def test_simple(usual_setup_usual_teardown):
    create_file('1234', 'a')
    create_file('1234', 'b')
    create_file('1234', 'stupid\'file,name')

    _, *_, _, csv_out = run_rmlint('-S a', outputs=('csv',))
    rows = csv_string_to_data(csv_out)

    assert [int(row['size']) for row in rows] == [4, 4, 4]

    assert [(row['type'], os.path.basename(row['path']), int(row['size'])) for row in rows] == [
        ('duplicate_file', 'a', 4),
        ('duplicate_file', 'b', 4),
        ('duplicate_file', "stupid'file,name", 4),
    ]

    assert all(set(row['checksum']) != {'0'} for row in rows)


# regression test for GitHub issue #496
def test_no_checksum(usual_setup_usual_teardown):
    # rmlint will not (normally) hash files with no same-sized siblings
    create_file('x', 'a')
    create_file('yy', 'b')

    # test for 'free(): invalid pointer' crash
    _, *data, _, csv_out = run_rmlint('-S a -c csv:unique', outputs=['csv'])
    assert not data

    # empty checksums should make it to output
    assert [r['checksum'] for r in csv_string_to_data(csv_out)] == [''] * 2
