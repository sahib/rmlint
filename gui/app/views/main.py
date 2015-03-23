#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import os
import math

# External:
from gi.repository import Gio
from gi.repository import Gtk
from gi.repository import GLib

# Internal:
from app.util import View, IndicatorLabel, ShredderPopupMenu, IconButton
from app.chart import ShredderChartStack
from app.runner import Runner

from app.cellrenderers import CellRendererSize
from app.cellrenderers import CellRendererModifiedTime
from app.cellrenderers import CellRendererCount
from app.cellrenderers import CellRendererLint


class ResultActionBar(Gtk.ActionBar):
    def __init__(self):
        Gtk.ActionBar.__init__(self)

        box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        box.get_style_context().add_class("linked")
        self.pack_start(box)

        icon_names = [
            'view-refresh-symbolic',
            'system-run-symbolic'
        ]

        for icon_name in icon_names:
            btn = Gtk.Button()
            btn.add(
                Gtk.Image.new_from_gicon(
                    Gio.ThemedIcon(name=icon_name),
                    Gtk.IconSize.BUTTON
                )
            )
            box.pack_start(btn, False, False, 0)

        self.script_btn = IconButton('printer-printing-symbolic', 'Render script')
        self.script_btn.get_style_context().add_class(
            Gtk.STYLE_CLASS_SUGGESTED_ACTION
        )
        self.pack_end(self.script_btn)

    def begin(self):
        pass

    def finish(self):
        self.script_btn.set_sensitive(True)
        self.script_btn.set_sensitive(True)
        self.script_btn.set_sensitive(True)



def _create_column(title, renderers, fixed_width=100):
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


class Column:
    """Column Enumeration to avoid using direct incides.
    """
    SELECTED, PATH, SIZE, COUNT, MTIME, TAG, TOOLTIP = range(7)


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

    #     yield iter_
    #     iter_ = model.iter_next(iter_)


def _ray(model, iter_):
    """Go down in hierarchy starting from iter_.
    Think of this as "shooting a ray down".
    """
    while iter_ is not None:
        yield iter_
        iter_ = model.iter_children(iter_)


def _is_lint_node(model, iter_):
    """
    """
    return model[iter_][Column.TAG] != IndicatorLabel.NONE


def _get_mtime_for_path(path):
    try:
        return os.stat(path).st_mtime
    except OSError:
        return 0


def _count_lint_nodes(model, iter_):
    """Count the number of real nodes below and including iter_.
    Real nodes are defined as nodes that represent lint and no
    intermediate directory.
    """
    cnt = int(_is_lint_node(model, iter_))
    for child in _dfs(model, model.iter_children(iter_)):
        # Check if the node is a duplicate file or directory
        # and no intermediate directory node
        cnt += _is_lint_node(model, iter_)

    return cnt


def _create_toggle_cellrenderer(model):
    renderer = Gtk.CellRendererToggle()

    def _mark_row(model, iter_, state):
        row = model[iter_]
        if row[Column.TAG] is IndicatorLabel.SUCCESS and state:
            row[Column.TAG] = IndicatorLabel.WARNING
        elif row[Column.TAG] is IndicatorLabel.WARNING and not state:
            row[Column.TAG] = IndicatorLabel.SUCCESS

        row[Column.SELECTED] = state

    def _recursive_flick(model, iter_, state):
        for child_iter in _dfs(model, iter_):
            _mark_row(model, child_iter, state)

    def _on_toggle(renderer, path):
        iter_ = model.get_iter_from_string(path)
        new_state = not model[iter_][Column.SELECTED]
        _mark_row(model, iter_, new_state)
        _recursive_flick(model, model.iter_children(iter_), new_state)

    renderer.connect('toggled', _on_toggle)
    return renderer


class ShredderTreeView(Gtk.TreeView):
    def __init__(self):
        Gtk.TreeView.__init__(self)

        self.md = Gtk.TreeStore(bool, str, int, int, int, int)

        self.set_grid_lines(Gtk.TreeViewGridLines.VERTICAL)
        self.set_fixed_height_mode(True)

        self.connect(
            'button-press-event',
            ShredderTreeView._on_button_press_event
        )

        # We handle tooltips ourselves
        self.set_has_tooltip(True)
        self.connect('query-tooltip', ShredderTreeView._on_query_tooltip)

        self._filter_query = None
        self.filter_model = self.md.filter_new()
        self.filter_model.set_visible_func(self._filter_func)

        self.set_model(self.filter_model)
        self.append_column(_create_column(
            'Path', [
                (_create_toggle_cellrenderer(self.filter_model), False, False, dict(active=0)),
                (CellRendererLint(), False, True, dict(text=1, tag=5))
            ],
            250
        ))
        self.append_column(_create_column(
            'Size', [(CellRendererSize(), True, False, dict(size=2))], 60
        ))
        self.append_column(_create_column(
            'Count', [(CellRendererCount(), True, False, dict(count=3))], 80
        ))
        self.append_column(_create_column(
            'Changed', [(CellRendererModifiedTime(), True, False, dict(mtime=4))], 100
        ))

        self.iter_map = {}

    def clear(self):
        self.filter_model.clear_cache()
        self.md.clear()

    def _on_button_press_event(self, event):
        # TODO: Actually implement all those.
        if event.button == 3:
            self.menu = ShredderPopupMenu()
            self.menu.simple_add('Toggle all', None)
            self.menu.simple_add('Toggle selected', None)
            self.menu.simple_add_separator()
            self.menu.simple_add('Open folder', None)
            self.menu.simple_add('Copy path to buffer', None)
            self.menu.simple_popup(event)

    def _on_query_tooltip(self, x, y, kb_mode, tooltip):
        # x and y is undefined if keyboard mode is used
        result, x, y, md, path, iter_ = self.get_tooltip_context(x, y, kb_mode)
        if not result:
            return False

        # Find the full path belonging to this row item.
        # We do this by iterating up. This way we trade memory usage
        # (storing the path a tooltip string) with cpu-time.
        tooltip_path = [md[iter_][Column.PATH]]
        while True:
            iter_ = md.iter_parent(iter_)
            if iter_ is None:
                break

            tooltip_path.insert(0, md[iter_][Column.PATH])

        # Set the tooltip text and where it should appear.
        self.set_tooltip_row(tooltip, path)
        tooltip.set_text('/'.join(tooltip_path))
        return True

    def refilter(self, filter_query):
        self._filter_query = filter_query
        self.filter_model.refilter()
        self.expand_all()

    def _filter_func(self, model, iter_, data):
        # TODO:
        # This is incorrect. We want to filter intermediate directories too,
        # if they have no (currently) visible children due to filtering.
        if model.iter_n_children(iter_) > 0:
            return True
        # for child in _ray(model, iter_):
        #     if _is_lint_node(model, child):
        #        break
        # else:
        #     print('ray', model[iter_][Column.PATH])
        #     # The for-loop ran without a break.
        #     return False

        if self._filter_query is not None:
            return self._filter_query in model[iter_][Column.PATH]

        # Show everything by default.
        return True

    def add_path(self, elem):
        GLib.idle_add(self._add_path_deferred, elem, len(elem.silbings))
        for twin in elem.silbings:
            GLib.idle_add(self._add_path_deferred, twin, len(elem.silbings))

    def _append_row(self, parent, checked, path, size, count, mtime, tag):
        return self.md.append(
            parent, (checked, path or '', size, count, mtime, tag)
        )

    def set_root(self, root_path):
        self.root_path = root_path
        self.iter_map[root_path] = self.root = self._append_row(
            None, False, os.path.basename(root_path),
            0, 0, _get_mtime_for_path(root_path), IndicatorLabel.THEME
        )

    def _add_path_deferred(self, elem, twin_count):
        folder = os.path.dirname(elem.path)
        if folder.startswith(self.root_path):
            folder = folder[len(self.root_path):]

        parts = folder.split('/')[1:] if folder else []
        parent = self.iter_map[self.root_path]

        for idx, part in enumerate(parts):
            part_path = os.path.join(*parts[:idx + 1])
            part_iter = self.iter_map.get(part_path)
            if part_iter is None:
                full_path = os.path.join(self.root_path, part_path)
                part_iter = self.iter_map[part_path] = self._append_row(
                    parent,
                    False, part, elem.size, 1,
                    _get_mtime_for_path(full_path), IndicatorLabel.NONE
                )
            else:
                # Update the intermediate directory:
                # TODO: Check if it is not a lint node?
                for row in self.md[part_iter], self.md[self.root]:
                    row[Column.COUNT] += 1
                    row[Column.SIZE] += elem.size

            parent = part_iter

        tag = IndicatorLabel.SUCCESS if elem.is_original else IndicatorLabel.ERROR
        node_iter = self._append_row(
            parent,
            not elem.is_original, os.path.basename(elem.path),
            elem.size, -twin_count, elem.mtime, tag
        )
        self.expand_to_path(self.md.get_path(node_iter))

        # Do not repeat this idle action.
        return False

    def finish_add(self):
        self.expand_all()
        self.columns_autosize()
        GLib.timeout_add(100, self.columns_autosize)


class MainView(View):
    def __init__(self, app):
        View.__init__(self, app, 'Step 2: Running...')

        # Disable scrolling for the main view:
        self.set_policy(
            Gtk.PolicyType.NEVER,
            Gtk.PolicyType.NEVER
        )

        self._is_running = False

        self.tv = ShredderTreeView()
        self.tv.set_halign(Gtk.Align.FILL)

        scw = Gtk.ScrolledWindow()
        scw.set_vexpand(True)
        scw.set_valign(Gtk.Align.FILL)
        scw.add(self.tv)

        self.chart_stack = ShredderChartStack()
        self.actionbar = ResultActionBar()

        stats_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        stats_box.pack_start(
            self.chart_stack, True, True, 0
        )
        stats_box.pack_start(
            ResultActionBar(), False, True, 0
        )

        stats_box.set_halign(Gtk.Align.FILL)
        stats_box.set_vexpand(True)
        stats_box.set_valign(Gtk.Align.FILL)

        separator = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        right_pane = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        right_pane.pack_start(separator, False, False, 0)
        right_pane.pack_start(stats_box, True , True, 0)

        grid = Gtk.Grid()
        grid.set_column_homogeneous(True)
        grid.attach(scw, 0, 0, 1, 1)
        grid.attach_next_to(right_pane, scw, Gtk.PositionType.RIGHT, 1, 1)

        self.add(grid)

        def _search_changed(entry):
            self.tv.refilter(entry.get_text() or None)

        self.app_window.search_entry.connect('search-changed', _search_changed)

    @property
    def is_running(self):
        return self._is_running

    def trigger_run(self, paths):
        self.tv.clear()

        root_path = '/usr/bin'
        self.tv.set_root(root_path)

        def _add_elem(runner, elem):
            self.tv.add_path(elem)

            tick = None if elem.progress is None else elem.progress / 100.0
            self.app_window.show_progress(tick)

        runner = Runner(self.app.settings, [root_path])
        runner.connect('lint-added', _add_elem)

        self.app_window.show_progress(0)

        def on_process_finish(runner, error_msg):
            self.app_window.show_progress(100)
            GLib.timeout_add(300, self.app_window.hide_progress)

            if error_msg is not None:
                self.app_window.show_infobar(
                    error_msg, message_type=Gtk.MessageType.WARNING
                )

            # Work on all idle sources first that were added.
            # This avoids some warnings and general race conditions
            # between different sources.
            while Gtk.events_pending():
                Gtk.main_iteration()

            self.chart_stack.set_visible_child_name(
                ShredderChartStack.DIRECTORY
            )
            self.tv.finish_add()

        runner.connect('process-finished', on_process_finish)

        # Start the process asynchronously
        runner.run()
