#!/usr/bin/env python
# encoding: utf-8

"""
GtkTreeModel Implementation for a directory tree and accompanying classes.

We use an own Model implementation instead of GtkTreeStore for sake of clean
code and more fine-grained control over the performance.

Since this is pure python the performance might not be 100% optimal but we try
to make the ui rendering as fluid as possible by making most of the costly
operations non-blocking.

Most of the code should be very clean but some vodoo is hidden.
If you don't find it, that's good. If you do, blame @sahib.
"""

# Stdlib:
import os
import time
import logging

from collections import OrderedDict, deque

# External:
from gi.repository import Gtk
from gi.repository import Gio
from gi.repository import GLib
from gi.repository import GObject

# Internal:
from shredder.util import CellRendererSize
from shredder.util import CellRendererModifiedTime
from shredder.util import CellRendererCount
from shredder.util import CellRendererLint
from shredder.util import PopupMenu, IndicatorLabel


LOGGER = logging.getLogger('tree')


# Unique ID used for PathTreeModel's GtkTreeIters:
# Useful for debugging, sometimes iters will
# not come from our own model and may be invalid.
PATH_MODEL_STAMP = 0xDEAD

# Process at max work items before letting the
# mainloop run for a short time. This is to prevent
# lagging in the user interface.
# This does not speed up the operation itself of course.
PATH_MODEL_CHUNK_SIZE = 500

# Wait this much ms before processing the next chunk.
PATH_MODEL_TIMEOUT_MS = 50


class Column:
    """Column Enumeration to avoid using direct indices.

    Only this class needs to be changed when adding/modifying columns.
    """
    SELECTED, SIZE, COUNT, MTIME, TAG, CKSUM, PATH, TOOLTIP = range(8)
    TYPES = [bool, str, int, int, int, int, str]

    @staticmethod
    def make_row(md_map):
        """Convert a rmlint json dict to a tree row"""
        is_original = md_map.get('is_original', False)
        if md_map.get('type', '').startswith('duplicate_'):
            if is_original:
                tag = IndicatorLabel.SUCCESS
            else:
                tag = IndicatorLabel.ERROR
        else:
            tag = IndicatorLabel.NONE

        # Use a list so we can update the counts and size later:
        # Note1: PATH and TOOLTIP are not included.
        #        Those are generated on demand.
        # Note2: Twins is negative, to differentiate between
        #        directory count and number of twins (!= 0)
        return [
            is_original,
            md_map.get('size', 0),
            -md_map.get('twins', 0),
            md_map.get('mtime', 0),
            tag,
            md_map.get('checksum', '')
        ]


class PathNode:
    """A single node in the PathTrie."""
    # Save a bit of memory:
    __slots__ = [
        # Public:
        'name', 'parent', 'children', 'row',

        # Private:
        'is_leaf', 'idx', 'indices', 'depth'
    ]

    def __init__(self, name, parent, metadata=None, children=None):
        # Public:
        self.name = name
        self.parent = parent
        self.children = children or OrderedDict()

        # If None probably a directory node, show the bare minimum:
        self.row = Column.make_row(metadata or {})

        # Private:
        self.indices = deque()
        self.idx = 0
        self.is_leaf = False
        self.depth = (parent.depth + 1) if parent else 0

    def __getitem__(self, idx):
        """Get a column value by it's column index.

        Some values might get freshly constructed on this call.
        """
        if idx is Column.PATH:
            return self.name
        elif idx is Column.TOOLTIP:
            return self.build_path()
        else:
            return self.row[idx]

    def append(self, name, is_leaf=False):
        """Append a node as child of this node.

        If is_leaf is True the size/count of all intermediate
        non-leaf nodes above are updated.
        """
        node = PathNode(name, self)
        node.idx = len(self.indices)

        self.children[name] = node
        self.indices.append(node)
        self.is_leaf = is_leaf

        return node

    def make_leaf(self, row):
        """Convert the node to a leaf node.

        Metadata will be copied to the node and intermediate
        directories above will be updated with count & size.
        """
        self.is_leaf = True
        self.row = row

        # Update intermediate directories top of it:
        for parent in (node for node in self.up() if not node.is_leaf):
            parent.row[Column.COUNT] += 1
            parent.row[Column.SIZE] += row[Column.SIZE]

    def up(self):
        """Iterate the trie up to root."""
        yield self
        if self.parent is not None:
            yield from self.parent.up()

    def match(self, query):
        """Check if a node matches query (simple string matching)"""
        for node in self.up():
            if query in node.name.lower():
                return True
        return False

    def build_path(self):
        """Recursively build the absolute path of this node"""
        return os.path.join(*reversed([n.name for n in self.up()]))

    def build_iter_path(self):
        """Recursively build a iter path suitable for GtkTreePath"""
        return list(reversed([n.idx for n in self.up()]))

    def neighbor(self, offset):
        """Get the neighbor of this node by a certain offset"""
        if self.parent is None:
            return None

        neighbor_idx = self.idx + offset
        if 0 <= neighbor_idx < len(self.parent.indices):
            return self.parent.indices[neighbor_idx]

        return None


def _create_root_path_index(index, path, node):
    """Create a (trie-like) recursive dict as fast root path lookup."""
    curr_map, last_map, name = index, None, ''

    for name in (comp for comp in path.split('/') if comp):
        last_map = curr_map
        curr_map = curr_map.setdefault(name, {})

    last_map[name] = node


def _lookup_root_path_index(index, components):
    """Lookup a node in the recursive dict by a split path,
    if found the node is returned and components is modified.
    """
    curr_map = index
    for idx, name in enumerate(components):
        curr_map = curr_map.get(name)
        if curr_map is None:
            return None

        if isinstance(curr_map, PathNode):
            del components[:idx + 1]
            return curr_map

    return None


class PathTrie:
    """Python version of rmlint's pathtricia trie."""

    def __init__(self, root_paths=None):
        self.root = PathNode('/', None, {})
        self.sub_roots = []
        self.nodes = {id(self.root): self.root}
        self.max_depth = 0

        self.root_paths = {}
        for root_path in root_paths or []:
            # Append the sub root node manually:
            sub_root_node = self.root.append(root_path.strip('/'))
            self.nodes[id(sub_root_node)] = sub_root_node
            self.sub_roots.append(sub_root_node)

            # Also add it to the "special" index.
            _create_root_path_index(self.root_paths, root_path, sub_root_node)

    def __iter__(self):
        return self.iterate(None)

    def __len__(self):
        return len(self.nodes)

    def __repr__(self):
        """Return a simple string version of the trie"""
        view = []
        for node in self:
            view.append((' ' * node.depth * 2) + node.name)

        return '\n'.join(view)

    def __getitem__(self, path):
        return self.find(path)

    def __setitem__(self, path, value):
        self.insert(path, value)

    def iterate(self, node=None):
        """Iterate trie down from node.
        If node is None, root is assumed;
        """
        node = node or self.root
        yield node

        for child in node.children.values():
            yield from self.iterate(child)

    def insert(self, path, row):
        """Insert a path into the trie, with metadata in `row`"""
        components = [comp for comp in path.split('/') if comp]
        curr = _lookup_root_path_index(
            self.root_paths,
            components) or self.root

        new_nodes = []

        for name in components:
            node = curr.children.get(name)
            if node is None:
                node = curr.append(name)
                self.nodes[id(node)] = node
                new_nodes.append((node, True))
            else:
                new_nodes.append((node, False))
            curr = node

        curr.make_leaf(row)
        self.max_depth = max(self.max_depth, curr.depth)
        return new_nodes

    def find(self, path):
        """Find a PathNode in the trie by it's path"""
        curr = self.root
        for name in (comp for comp in path.split('/') if comp):
            curr = curr.children.get(name)
            if curr is None:
                return None

        return curr

    def resolve(self, iter_path):
        """Resolve a list of indices to a node from root.
        The list might be for example [0, 0, 2] to go
        two layers down on the left and select the third
        node on this layer.

        The root path is located at [0].

        This is similar to GtkTreePath by purpose.
        """
        curr = self.root

        # [0] refers to root, strip first index:
        for idx in iter_path[1:]:
            curr = curr.indices[idx]

        return curr


def make_iter(node):
    """Make a GtkTreeIter, suitable for our PathTreeModel."""
    iter_ = Gtk.TreeIter()
    iter_.stamp = PATH_MODEL_STAMP
    iter_.user_data = id(node)
    return iter_


class PathTreeModel(GObject.GObject, Gtk.TreeModel):
    """Pack lint nodes into a tree structure compatible with Gtk

    This implements Gtk.TreeModel and can be therefore used as backend of
    Gtk.TreeView. It is backed up by a PathTrie as internal datastructure.

    Technical note:
        PyGObject prohibits storing pointers in Gtk.TreeIter.user_data.
        Therefore we store a hashtable with the id of our nodes and set the id
        as user_data as workaround. (see make_iter())
    """

    def __init__(self, paths):
        super(PathTreeModel, self).__init__()

        # Actual data storage:
        self.paths = paths
        self.trie = PathTrie(paths)

        # Manually insert the root path (we need one for working normally):
        path = Gtk.TreePath.new_from_indices([0])
        self.row_inserted(path, make_iter(self.trie.root))

        # Pack of files that are inserted to the trie at once.
        # This is a speed optimization to make the ui less blocking.
        self._file_pack, self._pack_timeout_id = [], None

        # Search optimization:
        # When typing '.pyo' after searching for '.py' we
        # can just filter the previous results.
        self._partial_model, self._last_query = None, None

        # Set of nodes that need to get updated periodically.
        # It would be expensive to do that on every insert
        # (causing one row redraw each), therefore we do it every second.
        self._intermediate_nodes = set()
        self._mtime_cache = set()
        GLib.timeout_add(1000, self._update_intermediate_nodes)

    def _update_intermediate_nodes(self):
        """Make sure the intermediate nodes get updated in a slow
        but sufficient interval.
        """
        self._intermediate_nodes.update(self.trie.sub_roots)
        self._intermediate_nodes.add(self.trie.root)

        for node in self._intermediate_nodes:
            indices = node.build_iter_path()
            path = Gtk.TreePath.new_from_indices(indices)

            if node not in self._mtime_cache:
                self._mtime_cache.add(node)
                full_path = node.build_path()

                # Attempt to read the mtime from file:
                try:
                    node.row[Column.MTIME] = os.stat(full_path).st_mtime
                except OSError:
                    pass

            self.row_changed(path, make_iter(node))

        # Reset for next time:
        self._intermediate_nodes = set()
        return True

    ########################
    #   Append Machinery   #
    ########################

    def add_path(self, path, row, immediately=False):
        """Add a path, including metadata, to the model.

        If immediately is False, the path will be cached and
        added after a small timeout as performance optimization.
        """
        if immediately:
            # Add it it immediately.
            # self.trie.insert(path, row)
            self._add_and_signal(path, row)
        else:
            # Defer the addition a bit more.
            self._file_pack.append((path, row))
            if self._pack_timeout_id is None:
                self._pack_timeout_id = GLib.timeout_add(
                    PATH_MODEL_TIMEOUT_MS, self._add_defer
                )

    def _add_and_signal(self, path, row):
        """Actually add the path and it's metadata here.
        Also signal the GtkTreeView to update if necessary.
        """
        parents = self.trie.insert(path, row)

        for node, was_new in parents:
            indices = node.build_iter_path()
            path = Gtk.TreePath.new_from_indices(indices)

            if was_new:
                self.row_inserted(path, make_iter(node))

            if not was_new:
                # Remember to update it somewhen later.
                self._intermediate_nodes.add(node)

    def _add_defer(self):
        """Add a pack of paths to the trie, max 500 at the same time."""
        LOGGER.info(
            'Adding pack: %d/%d',
            min(PATH_MODEL_CHUNK_SIZE, len(self._file_pack)),
            PATH_MODEL_CHUNK_SIZE
        )

        for path, row in self._file_pack[:PATH_MODEL_CHUNK_SIZE]:
            self._add_and_signal(path, row)

        self._file_pack = self._file_pack[PATH_MODEL_CHUNK_SIZE:]

        if self._file_pack:
            # If we had some leftovers we need to schedule a new _add_defer.
            self._pack_timeout_id = GLib.timeout_add(
                PATH_MODEL_TIMEOUT_MS, self._add_defer
            )
        else:
            # Finished for now, new paths might come later.
            self._pack_timeout_id = None

        return False

    def lookup_by_path(self, path):
        """Calls trie.find() to find the node attached to a path"""
        return self.trie.find(path)

    ##################################
    #     Filter Implementation      #
    ##################################

    def filter_model(self, query):
        """Filter the model (and thus update the view) by `query`.
        Instead of modifying the model, a new model is returned,
        which shows only contains the filtered nodes.

        This is surprisingly the most efficient method here, and comes with
        very little effort.  Small drawback: External code cannot rely on a
        single model
        """
        if not query:
            return self

        query = query.lower()
        partial_model = PathTreeModel(self.paths)

        # Find out which trie to filter.
        # If we had a search query with matching prefix before we can just
        # use the previous resulting model.
        base_trie = self.trie
        if self._partial_model and query.startswith(self._last_query):
            base_trie = self._partial_model.trie

        # Iterate over the trie; do not add unmatched.
        for node in base_trie:
            # For now we only search through leafs.
            if not node.is_leaf:
                continue

            # TODO: Not sure if we should search the full path...
            #       Maybe searching the leaf.name is enough for most usecases.
            #       (plus that would be faster too!)
            if not node.match(query):
                continue

            # Do not copy the rows, just ref them.
            path = node.build_path()
            partial_model.add_path(path, node.row, True)

        # Remember for the next step:
        self._partial_model, self._last_query = partial_model, query
        return partial_model

    ###################################
    # PyGObject convinience interface #
    ###################################

    def set_value(self, iter_, column, value):
        """Set the value of a cell.

        PyGObject seems to expect a method with this name,
        when setting the value of a row.
        """
        node = self.trie.nodes[iter_.user_data]
        node.row[column] = value

        # Find out which path to update:
        indices = node.build_iter_path()
        path = Gtk.TreePath.new_from_indices(indices)

        # Signal our change:
        self.row_changed(path, iter_)

    def __len__(self):
        return len(self.trie)

    ##################################
    # Tree Model Spec Implementation #
    ##################################

    def do_get_iter(self, path):
        """Returns a new TreeIter that points at path.

        The implementation returns a 2-tuple (bool, TreeIter|None).
        """
        node = self.trie.resolve(path.get_indices())

        if node is not None:
            return (True, make_iter(node))
        else:
            return (False, None)

    def _iter_move(self, iter_, offset):
        """Move iter_ by a certain offset."""
        node = self.trie.nodes[iter_.user_data]
        next_node = node.neighbor(offset)

        if next_node is None:
            return (False, None)
        else:
            iter_.user_data = id(next_node)
            return (True, iter_)

    def do_iter_next(self, iter_):
        """Returns an iter pointing to the next row or None.

        The implementation returns a 2-tuple (bool, TreeIter|None).
        """
        return self._iter_move(iter_, +1)

    def do_iter_previous(self, iter_):
        """Returns an iter pointing to the previous row or None.

        The implementation returns a 2-tuple (bool, TreeIter|None).
        """
        return self._iter_move(iter_, -1)

    def do_iter_parent(self, child_iter):
        """Returns an iter pointing to the parent of child_iter or None."""
        node = self.trie.nodes[child_iter.user_data]
        if node.parent:
            return (True, make_iter(node.parent))
        else:
            return (False, None)

    def do_iter_has_child(self, iter_):
        """True if iter has children."""
        return len(self.trie.nodes[iter_.user_data].children) > 0

    def do_iter_n_children(self, iter_):
        """Returns the number of children of iter_"""
        if iter_ is None:
            return 0
        else:
            return len(self.trie.nodes[iter_.user_data].children)

    def do_iter_children(self, parent):
        """Return first child or (False|None)"""
        return self.do_iter_nth_child(parent, 0)

    def do_iter_nth_child(self, parent, nth):
        """Return iter that is set to the nth child of iter."""
        if parent is None:
            node = self.trie.root
        elif parent.user_data is 0:
            # Sometimes an invalid iterator lands here.
            # It has no user_data and an invalid stamp field.
            return (False, None)
        else:
            node = self.trie.nodes[parent.user_data]

        if nth < len(node.indices):
            return (True, make_iter(node.indices[nth]))

        return (False, None)

    def do_get_path(self, iter_):
        """Returns tree path references by iter."""
        node = self.trie.nodes[iter_.user_data]
        return Gtk.TreePath(reversed([parent.idx for parent in node.up()]))

    def do_get_value(self, iter_, column):
        """Returns the value for iter and column."""
        node = self.trie.nodes[iter_.user_data]
        return node[column]

    def do_get_n_columns(self):
        """Returns the number of columns."""
        return len(Column.TYPES)

    def do_get_column_type(self, column_idx):
        """Returns the type of the column."""
        return Column.TYPES[column_idx]

    def do_get_flags(self):
        """Returns the flags supported by this interface."""
        return Gtk.TreeModelFlags.ITERS_PERSIST

    ########################
    # Misc utility methods #
    ########################

    def iter_to_node(self, iter_):
        """Convert a GtkTreeIter to the related PathNode"""
        return self.trie.nodes.get(iter_.user_data)


def _create_column(title, renderers, fixed_width=100):
    """Convinience method for creating a TreeView Column.
    Several renderers can be given with certain options.
    """
    column = Gtk.TreeViewColumn()
    column.set_title(title)
    column.set_resizable(True)
    column.set_sizing(Gtk.TreeViewColumnSizing.FIXED)
    column.set_fixed_width(fixed_width)

    for renderer, pack_end, expand, kwargs in renderers:
        renderer.set_alignment(1.0 if pack_end else 0.0, 0.5)
        pack_func = column.pack_end if pack_end else column.pack_start
        pack_func(renderer, expand)
        column.set_attributes(cell_renderer=renderer, **kwargs)
        column.set_expand(expand)

    return column


def _dfs(model, iter_):
    """Generator for a depth first traversal in a TreeModel.
    Yields a GtkTreeIter for all iters below and after iter_.
    """
    while iter_ is not None:
        child = model.iter_children(iter_)
        if child is not None:
            yield from _dfs(model, child)

        yield iter_
        iter_ = model.iter_next(iter_)


def _mark_row(model, child_iter, state):
    """Check if we need to update the tag icon"""
    row = model[child_iter]
    if row[Column.TAG] is IndicatorLabel.SUCCESS and state:
        row[Column.TAG] = IndicatorLabel.WARNING
    elif row[Column.TAG] is IndicatorLabel.WARNING and not state:
        row[Column.TAG] = IndicatorLabel.SUCCESS

    row[Column.SELECTED] = state


def _recursive_flick(model, iter_, state):
    """Propapgate flick in a depth first manner"""
    for child_iter in _dfs(model, iter_):
        _mark_row(model, child_iter, state)


def on_toggle(_, path, treeview):
    """Toggle renderer state and it's children"""
    model = treeview.get_model()
    iter_ = model.get_iter_from_string(path)
    new_state = not model[iter_][Column.SELECTED]
    _mark_row(model, iter_, new_state)
    _recursive_flick(model, model.iter_children(iter_), new_state)


def _create_toggle_cellrenderer(treeview):
    """Return a normal Gtk.CellRendererToggle, but also toggle childs."""
    renderer = Gtk.CellRendererToggle()
    renderer.connect('toggled', on_toggle, treeview)
    return renderer


class PathTreeView(Gtk.TreeView):
    """A GtkTreeView that is readily configured for using PathTreeModel"""

    def __init__(self):
        Gtk.TreeView.__init__(self)

        # Enable separator lines:
        self.set_grid_lines(Gtk.TreeViewGridLines.NONE)
        self.set_enable_tree_lines(True)

        # Small spedup:
        self.set_fixed_height_mode(True)

        # Enable querying of the tooltip column:
        self.set_tooltip_column(Column.TOOLTIP)

        # Configure the column rendering:
        self.append_column(_create_column(
            'Path', [
                (CellRendererLint(), False, False, dict(tag=Column.TAG)),
                # (
                #     _create_toggle_cellrenderer(self),
                #     False, False, dict(active=Column.SELECTED)
                # ),
                (Gtk.CellRendererText(), False, True, dict(text=Column.PATH)),
            ],
            250
        ))
        self.append_column(_create_column(
            'Size',
            [(CellRendererSize(), True, False, dict(size=Column.SIZE))],
            80
        ))
        self.append_column(_create_column(
            'Count',
            [(CellRendererCount(), True, False, dict(count=Column.COUNT))],
            100
        ))
        self.append_column(
            _create_column(
                'Changed', [
                    (CellRendererModifiedTime(), True, False, dict(
                        mtime=Column.MTIME))], 110))

        self.connect(
            'button-press-event',
            PathTreeView.on_button_press_event
        )

        self._menu = None

    def set_model(self, model):
        """Overwrite Gtk.TreeView.set_model, but expand sub root paths"""
        Gtk.TreeView.set_model(self, model)
        self.expand_all()

    def on_button_press_event(self, event):
        """Callback handler only used for mouse clicks."""
        # TODO: Actually implement all those.
        if event.button != 3:
            return

        # HACK: bind to self, since the ref would get lost.
        self._menu = PopupMenu()
        self._menu.simple_add('Toggle all', None)
        self._menu.simple_add('Toggle selected', None)
        self._menu.simple_add_separator()
        self._menu.simple_add('Open folder', None)
        self._menu.simple_add('Copy path to buffer', None)
        self._menu.simple_popup(event)


if __name__ == '__main__':
    def main():
        import sys

        model = PathTreeModel(sys.argv[1:])
        for arg_path in sys.argv[1:]:
            model.add_path(
                arg_path, Column.make_row({'mtime': time.time(), 'size': 0}))

        from shredder.runner import Runner
        settings = Gio.Settings.new('org.gnome.Shredder')

        runner = Runner(settings, sys.argv[1:])
        runner.connect(
            'lint-added',
            lambda _: model.add_path(
                runner.element['path'],
                Column.make_row(
                    runner.element)))

        runner.connect(
            'process-finished',
            lambda _,
            msg: print(
                'Status:',
                msg))
        runner.run()

        view = PathTreeView()
        view.set_model(model)

        runner.connect(
            'process-finished',
            lambda _,
            msg: GLib.timeout_add(
                500,
                view.expand_all))

        def _search_changed(entry):
            view.set_model(model.filter_model(entry.get_text()))
            view.expand_all()

        entry = Gtk.SearchEntry()
        entry.connect('search-changed', _search_changed)

        scw = Gtk.ScrolledWindow()
        scw.add(view)

        win = Gtk.Window()
        win.set_default_size(640, 480)
        win.connect('destroy', Gtk.main_quit)

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        box.pack_start(scw, True, True, 0)
        box.pack_start(entry, False, False, 0)

        win.add(box)
        win.show_all()

        Gtk.main()

    main()
