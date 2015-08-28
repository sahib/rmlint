#!/usr/bin/env python
# encoding: utf-8

"""
Main view of Shredder.

Shows the chart and a treeview of suspicious files.
"""

# Stdlib:
import logging

# External:
from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import Gio
from gi.repository import GLib
from gi.repository import GObject

# Internal:
from shredder.util import View, IconButton, PopupMenu, IndicatorLabel
from shredder.chart import ChartStack
from shredder.tree import PathTreeView, PathTreeModel, Column, PathTrie
from shredder.runner import Runner


LOGGER = logging.getLogger('runview')


class ResultActionBar(Gtk.ActionBar):
    """Down right bar with the controls"""
    __gsignals__ = {
        'generate-script': (GObject.SIGNAL_RUN_FIRST, None, ()),
        'partial-generate-script': (GObject.SIGNAL_RUN_FIRST, None, ())
    }

    def __init__(self, view):
        Gtk.ActionBar.__init__(self)

        box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        box.get_style_context().add_class("linked")
        self.pack_start(box)

        self.refresh_button = IconButton('view-refresh-symbolic')
        self.settings_button = IconButton('system-run-symbolic')

        self.refresh_button.connect(
            'clicked', lambda _: view.app_window.views['runner'].rerun()
        )
        self.settings_button.connect(
            'clicked', lambda _: view.app_window.views.switch('settings')
        )

        box.pack_start(self.refresh_button, False, False, 0)
        box.pack_start(self.settings_button, False, False, 0)

        self.script_btn = IconButton(
            'printer-printing-symbolic', 'Render script'
        )
        self.script_btn.get_style_context().add_class(
            Gtk.STYLE_CLASS_SUGGESTED_ACTION
        )
        self.script_btn.connect(
            'clicked', lambda _: self.emit('generate-script')
        )

        self.script_btn.set_sensitive(False)

        self.partial_script_btn = IconButton(
            'printer-printing-symbolic', 'Visible to script'
        )
        self.partial_script_btn.connect(
            'clicked', lambda _: self.emit('partial-generate-script')
        )
        self.partial_script_btn.set_sensitive(False)

        self.pack_end(self.script_btn)
        self.pack_end(self.partial_script_btn)

    def activate_script_btn(self, mode):
        self.script_btn.set_sensitive(mode)

    def activate_partial_script_btn(self, mode):
        self.partial_script_btn.set_sensitive(mode)

    def is_active(self):
        return self.script_btn.is_sensitive()


class RunnerView(View):
    """Main action View.

    Public attributes:

        - script: A Script instance.
        - runner: The current run-instance.
        - model: The data.
    """
    def __init__(self, app):
        View.__init__(self, app, 'Running…')

        # Public: The runner.
        self.runner = None

        self.last_paths = []

        # Disable scrolling for the main view:
        self.scw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.NEVER)

        # Public flag for checking if the view is still
        # in running mode (thus en/disabling certain features)
        self.is_running = False

        self._script_generated = False

        self.model = PathTreeModel([])
        self.treeview = PathTreeView()
        self.treeview.set_model(self.model)
        self.treeview.set_halign(Gtk.Align.FILL)
        self.treeview.get_selection().connect(
            'changed',
            self.on_selection_changed
        )

        for column in self.treeview.get_columns():
            column.connect(
                'clicked',
                lambda _: self.rerender_chart()
            )

        # Scrolled window on the left
        scw = Gtk.ScrolledWindow()
        scw.set_vexpand(True)
        scw.set_valign(Gtk.Align.FILL)
        scw.add(self.treeview)

        self.chart_stack = ChartStack()
        self.actionbar = ResultActionBar(self)

        # Right part of the view
        stats_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        stats_box.pack_start(self.chart_stack, True, True, 0)
        stats_box.pack_start(self.actionbar, False, True, 0)
        stats_box.set_halign(Gtk.Align.FILL)
        stats_box.set_vexpand(True)
        stats_box.set_valign(Gtk.Align.FILL)

        # Separator container for separator|chart (could have used grid)
        separator = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        right_pane = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        right_pane.pack_start(separator, False, False, 0)
        right_pane.pack_start(stats_box, True, True, 0)

        grid = Gtk.Grid()
        grid.set_column_homogeneous(True)
        grid.attach(scw, 0, 0, 1, 1)
        grid.attach_next_to(right_pane, scw, Gtk.PositionType.RIGHT, 1, 1)

        self.add(grid)

        self.search_entry.connect(
            'search-changed', self.on_search_changed
        )

        self.actionbar.connect(
            'generate-script', self.on_generate_script
        )

        self.actionbar.connect(
            'partial-generate-script', self.on_generate_partial_script
        )

        self._menu = None
        self.treeview.connect('show-menu', self.on_show_menu)

    def reset(self):
        """Reset internally to freshly initialized."""
        self.is_running = False
        self._script_generated = False
        self.runner = None
        self.last_paths = []

        self.chart_stack.set_visible_child_name(ChartStack.LOADING)

    def trigger_run(self, untagged_paths, tagged_paths):
        """Trigger a new run on all paths in `paths`"""
        # Remember last paths for rerun()
        self.reset()
        self.last_paths = (untagged_paths, tagged_paths)

        # Make sure it looks busy:
        self.sub_title = 'Running…'

        # Fork off the rmlint process:
        self.runner = Runner(self.app.settings, untagged_paths, tagged_paths)
        self.runner.connect('lint-added', self.on_add_elem)
        self.runner.connect('process-finished', self.on_process_finish)
        self.runner.run()

        # Make sure the previous run is not visible anymore:
        self.model = PathTreeModel([])
        self.treeview.set_model(self.model)

        # Indicate that we're in a fresh run:
        self.is_running = True
        self.show_progress(0)

    def rerun(self):
        """Rerun with last given paths."""
        self.trigger_run(*self.last_paths)

    ###########################
    #     SIGNAL CALLBACKS    #
    ###########################

    def on_search_changed(self, entry):
        """Called once the user entered a new query."""
        text = entry.get_text()

        sub_model = self.model.filter_model(text)
        if sub_model is not self.treeview.get_model():
            self.chart_stack.render(sub_model.trie.root)
            self.treeview.set_model(sub_model)
            self.actionbar.activate_partial_script_btn(True)
        else:
            self.actionbar.activate_partial_script_btn(False)

    def on_add_elem(self, runner):
        """Called once the runner found a new element."""
        elem = runner.element
        self.model.add_path(elem['path'], Column.make_row(elem))

        # Decide how much progress to show (or just move a bit)
        tick = (elem.get('progress', 0) / 100.0) or None
        self.show_progress(tick)

    def on_process_finish(self, _, error_msg):
        """Called once self.runner finished running."""
        # Make sure we end up at 100% progress and show
        # the progress for a short time after (for the nice cozy feeling)
        LOGGER.info('`rmlint` finished.')
        self.show_progress(100)
        GLib.timeout_add(300, self.hide_progress)
        GLib.timeout_add(350, self.treeview.expand_all)

        self.sub_title = 'Finished scanning.'

        if error_msg is not None:
            self.app_window.show_infobar(
                error_msg, message_type=Gtk.MessageType.WARNING
            )

        GLib.timeout_add(1000, self.on_delayed_chart_render, -1)

    def on_delayed_chart_render(self, last_size):
        """Called after a short delay to reduce chart redraws."""
        model = self.treeview.get_model()
        current_size = len(model)

        if current_size == last_size:
            # Come back later:
            return False

        if len(model) > 1:
            self.chart_stack.set_visible_child_name(ChartStack.CHART)
            self.rerender_chart()
            self.actionbar.activate_script_btn(True)
        else:
            self.chart_stack.set_visible_child_name(ChartStack.EMPTY)

        GLib.timeout_add(1500, self.on_delayed_chart_render, current_size)

        return False

    def rerender_chart(self):
        """Re-render the chart from the current model root."""
        LOGGER.info('Refreshing chart.')
        model = self.treeview.get_model()
        self.chart_stack.render(model.trie.root)

    def on_view_enter(self):
        """Called when the view enters sight."""
        GLib.idle_add(
            lambda: self.app_window.views.go_right.set_sensitive(
                self._script_generated
            )
        )

    def on_view_leave(self):
        """Called when the view leaves sight."""
        self.app_window.views.go_right.set_sensitive(True)

    def on_selection_changed(self, selection):
        """Called when the user clicks a specific row."""
        node = self.treeview.get_selected_node()
        if node is None:
            return

        if not node.children:
            # It is a single file.
            # Show a chart containing all twins of this file.
            # This is helpful to see quickly where those lie.

            cksum = node[Column.CKSUM]
            group = self.model.trie.group(cksum)
            group_trie = PathTrie()

            for twin_node in group or []:
                group_trie.insert(
                    twin_node.build_path(),
                    node.row
                )

            self.chart_stack.render(group_trie.root)
        else:
            self.chart_stack.render(node)

    def _generate_script(self, model):
        self._script_generated = True

        trie = model.trie
        self.runner.replay({
            ch.build_path(): ch[Column.SELECTED] for ch in trie if ch.is_leaf
        })

        self.app_window.views.go_right.set_sensitive(True)
        self.app_window.views.switch('editor')

    def on_generate_script(self, _):
        self._generate_script(self.model)

    def on_generate_partial_script(self, _):
        self._generate_script(self.treeview.get_model())

    def on_default_action(self):
        """Called on Ctrl-Enter"""
        if self.actionbar.is_active():
            self._generate_script(self.model)

    #######################
    # MENU ENTRY HANDLING #
    #######################

    def on_show_menu(self, _):
        # HACK: bind to self, since the ref would get lost.
        self._menu = PopupMenu()
        self._menu.simple_add('Toggle all', self.on_toggle_all)
        self._menu.simple_add('Toggle selected', self.on_toggle_selected)
        self._menu.simple_add_separator()
        self._menu.simple_add('Expand all', self.on_expand_all)
        self._menu.simple_add('Collapse all', self.on_collapse_all)
        self._menu.simple_add_separator()
        self._menu.simple_add('Open item', self.on_open_folder)
        self._menu.simple_add(
            'Copy path to clipboard',
            self.on_copy_to_clipboard
        )
        return self._menu

    def on_open_folder(self, _):
        node = self.treeview.get_selected_node()
        if node is None:
            return

        try:
            LOGGER.info('Calling xdg-open %s', node.build_path())
            Gio.Subprocess.new(
                ['xdg-open', node.build_path()], 0
            )
        except GLib.Error as err:
            LOGGER.exception('Could not open directory via xdg-open')
            self.app_window.show_infobar(str(err))

    def on_copy_to_clipboard(self, _):
        node = self.treeview.get_selected_node()
        if node is None:
            return

        path = node.build_path()

        clipboard = Gtk.Clipboard.get_default(Gdk.Display.get_default())
        clipboard.set_text(path, len(path))

    def _toggle_tag_state(self, node_iter):
        model = self.treeview.get_model()
        for node in node_iter:
            current, new = node[Column.TAG], IndicatorLabel.NONE

            if current is IndicatorLabel.ERROR:
                new = IndicatorLabel.SUCCESS
            elif current is IndicatorLabel.SUCCESS:
                new = IndicatorLabel.ERROR

            model.set_node_value(node, Column.TAG, new)

    def on_toggle_all(self, _):
        self._toggle_tag_state(self.model.trie)

    def on_toggle_selected(self, _):
        # TODO: This is dead ugly, fix.
        nodes = list(self.treeview.get_selected_nodes())
        self._toggle_tag_state(nodes)

        # Note: this is the *full* trie.
        trie = self.model.trie

        for node in nodes:
            # Json documents with all related twins:
            group = trie.group(node[Column.CKSUM])
            if not group:
                continue

            # List of PathNodes which are twins:
            has_original = False
            for twin_node in group:
                if twin_node[Column.TAG] is IndicatorLabel.SUCCESS:
                    has_original = True
                    break

            # All good:
            if has_original:
                continue

            # No original in group, set first twin as original.
            for twin_node in group:
                if twin_node is not node:
                    self.model.set_node_value(
                        twin_node,
                        Column.TAG,
                        IndicatorLabel.SUCCESS
                    )
                    break

    def on_expand_all(self, _):
        self.treeview.expand_all()

    def on_collapse_all(self, _):
        self.treeview.collapse_all()
