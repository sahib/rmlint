from nose import with_setup
from tests.utils import *

#import time
#import itertools
#import string




@with_setup(usual_setup_func, usual_teardown_func)
def test_km():
    # create some dupes with different paths, names and mtimes:
    create_file('xxx', 'stuff/a')
    create_file('yyy', 'stuff/b')
    create_file('yyy', 'stuff/b_dupe')
    create_file('xxx', 'morestuff/a_copy')
    create_file('yyy', 'morestuff/b_copy')
    create_file('xxx', 'backup/a_backup')
    create_file('xxx', 'backup/a_copy_backup')
    create_file('yyy+', 'backup/b_backup_changed')
    create_file('zzz', 'backup/d')
    create_file('zzz', 'backup/d_copy')
    
    # search path with backup folder tagged
    search_paths = TESTDIR_NAME + '/ // ' + TESTDIR_NAME + '/backup'
    
    # 1. normal case - should find all dupes
    head, *data, footer = run_rmlint(search_paths, use_default_dir=False)
    assert len(data) == 4 + 2 + 3  # 4*'xxx' + 2*'zzz + 3*'yyy'
    assert footer['duplicates'] == (4-1) + (2-1) + (3-1) 
    
    # 2. --keep-all-tagged case - should ignore 'zzz' pair since they are both tagged
    #                          should also treat 2 of the 'xxx' as originals since two are tagged
    head, *data, footer = run_rmlint('-k ' + search_paths, use_default_dir=False)
    assert len(data) == 4 + 3
    assert footer['duplicates'] == (4-2) + (3-1)
    for afile in data:
        if afile['type'] == 'duplicate_file' and afile['is_original'] == False:
            # check no tagged files marked as duplicates
            assert not ('backup/' in afile['path'])

    # 3. --keep-all-untagged case - should ignore 'yyy' triple since they are all untagged
    #                          should also treat 2 of the 'xxx' as originals since two are untagged
    head, *data, footer = run_rmlint(' -K ' + search_paths, use_default_dir=False)
    assert len(data) == 4 + 2
    assert footer['duplicates'] == (4-2) + (2-1)
    for afile in data:
        if afile['type'] == 'duplicate_file' and afile['is_original'] == False:
            # check no untagged files marked as duplicates
            assert 'backup/' in afile['path']

    # 4. --must-match-tagged case - should ignore 'yyy' triple since they have no tagged copies
    head, *data, footer = run_rmlint(' -m ' + search_paths, use_default_dir=False)
    assert len(data) == 4 + 2
    assert footer['duplicates'] == (4-1) + (2-1)

    # 5. --must-match-untagged case - should ignore 'zzz' pair since they have no untagged copies
    head, *data, footer = run_rmlint(' -M ' + search_paths, use_default_dir=False)
    assert len(data) == 4 + 3
    assert footer['duplicates'] == (4-1) + (3-1)

    # 6. -km case:
    #     should ignore 'zzz' pair since they are both tagged
    #     should ignore 'yyy' triple since they have no tagged copies
    #     should also treat 2 of the 'xxx' as originals since two are tagged
    head, *data, footer = run_rmlint(' -km ' + search_paths, use_default_dir=False)
    assert len(data) == 4
    assert footer['duplicates'] == (4-2)
    for afile in data:
        if afile['type'] == 'duplicate_file' and afile['is_original'] == False:
            # check no tagged files marked as duplicates
            assert not ('backup/' in afile['path'])

    # 7. -KM case:
    #     should ignore 'yyy' triple since they are all untagged
    #     should ignore 'zzz' pair since they have no untagged copies
    #     should also treat 2 of the 'xxx' as originals since two are untagged
    head, *data, footer = run_rmlint(' -KM ' + search_paths, use_default_dir=False)
    assert len(data) == 4
    assert footer['duplicates'] == (4-2)
    for afile in data:
        if afile['type'] == 'duplicate_file' and afile['is_original'] == False:
            # check no tagged files marked as duplicates
            assert ('backup/' in afile['path'])

