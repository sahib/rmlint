from nose import with_setup
from tests.utils import *

numpairs = 1024 + 1

def branch_tree(current_path, remaining_depth):
    if (remaining_depth > 0):
        for i in range(2):
            next_path = current_path + "long" * 4 + str(i) + "/"
            create_dirs (next_path)
            branch_tree (next_path, remaining_depth - 1)
    else:
        for i in range(numpairs):
            create_file(str(i).zfill(1 + i), current_path + 'a' + str(i).zfill(7))
            create_file(str(i).zfill(1 + i), current_path + 'b' + str(i).zfill(7))
            create_link(current_path + 'a' + str(i).zfill(7), current_path + 'c' + str(i).zfill(7))
            create_link(current_path + 'a' + str(i).zfill(7), current_path + 'd' + str(i).zfill(7))
            create_link(current_path + 'a' + str(i).zfill(7), current_path + 'e' + str(i).zfill(7))
            create_link(current_path + 'a' + str(i).zfill(7), current_path + 'f' + str(i).zfill(7))
            create_link(current_path + 'b' + str(i).zfill(7), current_path + 'g' + str(i).zfill(7))
            create_link(current_path + 'b' + str(i).zfill(7), current_path + 'h' + str(i).zfill(7))

@with_setup(usual_setup_func, usual_teardown_func)
def test_manylongpathfiles():
    max_depth = 10 # will give 8M files total
    
    branch_tree ("", max_depth)

    head, *data, footer = run_rmlint('')
    assert len(data) == numpairs * 2 ** max_depth * 8
