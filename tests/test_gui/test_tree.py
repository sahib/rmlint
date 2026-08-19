"""Tests for shredder.tree"""
from tests.test_gui import helpers

helpers.import_shredder()

from shredder.tree import Column, PathTreeModel, PathTrie


def test_trie_sort_by_size():
    trie = PathTrie()

    for name, size in [('big', 300), ('small', 10), ('mid', 100)]:
        trie.insert('/' + name, Column.make_row({'size': size}))

    list(trie.sort(Column.SIZE))
    assert [node.name for node in trie.root.indices] == ['small', 'mid', 'big']

    list(trie.sort(Column.SIZE, reverse=True))
    assert [node.name for node in trie.root.indices] == ['big', 'mid', 'small']


def test_model_sort():
    from gi.repository import Gtk

    model = PathTreeModel([])
    model.add_path('/test/bigger', Column.make_row({'size': 84}), immediately=True)
    model.add_path('/test/smaller', Column.make_row({'size': 42}), immediately=True)

    model.sort(Column.SIZE, Gtk.SortType.ASCENDING)
    s_node = model.lookup_by_path('/test')
    assert [node.name for node in s_node.indices] == ['smaller', 'bigger']

    model.sort(Column.SIZE, Gtk.SortType.DESCENDING)
    assert [node.name for node in s_node.indices] == ['bigger', 'smaller']
