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
from gi.repository import Gio
from gi.repository import GLib
from gi.repository import GObject

# Internal:
from shredder.util import View, IconButton, PopupMenu, IndicatorLabel
from shredder.util import MultipleChoiceButton, scrolled
from shredder.chart import ChartStack
from shredder.tree import PathTreeView, PathTreeModel, Column, PathTrie
from shredder.runner import Runner


LOGGER = logging.getLogger('runview')


class ResultActionBar(Gtk.ActionBar):
    """Down right bar with the controls"""
    __gsignals__ = {
        'generate-all-script': (GObject.SIGNAL_RUN_FIRST, None, ()),
        'generate-filtered-script': (GObject.SIGNAL_RUN_FIRST, None, ()),
        'generate-selection-script': (GObject.SIGNAL_RUN_FIRST, None, ())
    }

    def __init__(self, view):
        Gtk.ActionBar.__init__(self)

        left_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        left_box.get_style_context().add_class("linked")
        self.pack_start(left_box)

        self.refresh_button = IconButton('view-refresh-symbolic')
        self.settings_button = IconButton('system-run-symbolic')

        self.refresh_button.connect(
            'clicked', lambda _: view.app_window.views['runner'].rerun()
        )
        self.settings_button.connect(
            'clicked', lambda _: view.app_window.views.switch('settings')
        )

        left_box.pack_start(self.refresh_button, False, False, 0)
        left_box.pack_start(self.settings_button, False, False, 0)

        right_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        right_box.get_style_context().add_class(
            Gtk.STYLE_CLASS_LINKED
        )

        self.script_btn = IconButton(
            'emblem-documents-symbolic', 'Render script from'
        )
        self.script_btn.connect(
            'clicked', self.on_generate_script
        )

        self.script_type_btn = MultipleChoiceButton(
            ['All', 'Filtered', 'Selected'],
            'All',
            'All',
            'What part of the results to generate the script from'
        )
        self.script_type_btn.set_relief(Gtk.ReliefStyle.NORMAL)

        right_box.pack_start(self.script_btn, True, True, 0)
        right_box.pack_start(self.script_type_btn, True, False, 0)

        self.pack_end(right_box)
        self.set_sensitive(False)

    def on_generate_script(self, _):
        choice = self.script_type_btn.get_selected_choice().lower()

        if choice == 'all':
            self.emit('generate-all-script')
        elif choice == 'filtered':
            self.emit('generate-filtered-script')
        elif choice == 'selected':
            self.emit('generate-selection-script')
        else:
            LOGGER.error('Bug: bad choice selection: %s', choice)

    def set_sensitive(self, mode):
        btn_ctx = self.script_btn.get_style_context()
        type_ctx = self.script_type_btn.get_style_context()

        if mode:
            btn_ctx.add_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)
            type_ctx.add_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)
        else:
            btn_ctx.remove_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)
            type_ctx.remove_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)

        self.script_btn.set_sensitive(mode)
        self.script_type_btn.set_sensitive(mode)

    def is_sensitive(self):
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
        self.treeview.get_selection().connect(
            'changed',
            self.on_selection_changed
        )

        self.group_treeview = PathTreeView()
        self.group_treeview.set_vexpand(True)
        self.group_treeview.set_valign(Gtk.Align.FILL)

        group_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        group_box.pack_start(scrolled(self.group_treeview), True, True, 0)
        group_box.pack_start(Gtk.HSeparator(), False, False, 0)

        self.group_revealer = Gtk.Revealer()
        self.group_revealer.set_transition_type(Gtk.RevealerTransitionType.SLIDE_DOWN)
        self.group_revealer.set_vexpand(True)
        self.group_revealer.set_valign(Gtk.Align.FILL)
        self.group_revealer.add(group_box)
        self.group_revealer.set_no_show_all(True)
        self.group_revealer.set_size_request(-1, 70)

        for column in self.treeview.get_columns():
            column.connect(
                'clicked',
                lambda _: self.rerender_chart()
            )

        self.chart_stack = ChartStack()
        self.actionbar = ResultActionBar(self)
        self.actionbar.set_valign(Gtk.Align.END)
        self.actionbar.set_halign(Gtk.Align.FILL)

        # Right part of the view
        stats_box = Gtk.Paned(orientation=Gtk.Orientation.VERTICAL)
        stats_box.pack1(self.group_revealer, True, True)
        stats_box.pack2(self.chart_stack, True, True)
        stats_box.props.position = 200

        # Separator container for separator|chart (could have used grid)
        separator = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        right_pane = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        right_pane.pack_start(separator, False, False, 0)
        right_pane.pack_start(stats_box, True, True, 0)
        right_pane.set_size_request(100, -1)

        scw = scrolled(self.treeview)
        scw.set_size_request(200, -1)

        paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        paned.set_vexpand(True)
        paned.set_valign(Gtk.Align.FILL)
        paned.pack1(scw, True, True)
        paned.pack2(right_pane, True, True)
        paned.props.position = 720
        paned.set_hexpand(True)

        grid = Gtk.Grid()
        grid.attach(paned, 0, 0, 1, 1)
        grid.attach(self.actionbar, 0, 1, 1, 1)

        self.add(grid)

        self.search_entry.connect(
            'search-changed', self.on_search_changed
        )

        self.actionbar.connect(
            'generate-all-script', self.on_generate_script
        )
        self.actionbar.connect(
            'generate-filtered-script', self.on_generate_filtered_script
        )
        self.actionbar.connect(
            'generate-selection-script', self.on_generate_selection_script
        )

        self._menu = None

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
        self.model = PathTreeModel(untagged_paths + tagged_paths)
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

        if model.trie.has_leaves():
            self.chart_stack.set_visible_child_name(ChartStack.CHART)
            self.rerender_chart()
            self.actionbar.set_sensitive(True)
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

            group_model = PathTreeModel(
                self.last_paths[0] + self.last_paths[1]
            )

            for twin_node in group or []:
                group_model.add_path(
                    twin_node.build_path(),
                    twin_node.row,
                    immediately=True
                )

            self.group_treeview.set_model(group_model)
            self.group_revealer.show()
            self.group_revealer.get_child().show_all()
            self.group_revealer.set_reveal_child(True)
            self.chart_stack.render(group_model.trie.root)
        else:
            self.group_revealer.hide()
            self.group_revealer.set_reveal_child(False)
            self.chart_stack.render(node)

    def _generate_script(self, trie, node):
        self._script_generated = True

        iterator = trie.iterate(node=node)
        self.runner.replay({
            ch.build_path(): ch[Column.SELECTED] for ch in iterator if ch.is_leaf
        })

        self.app_window.views.go_right.set_sensitive(True)
        self.app_window.views.switch('editor')

    def on_generate_script(self, _):
        self._generate_script(self.model.trie, self.model.trie.root)

    def on_generate_filtered_script(self, _):
        model = self.treeview.get_model()
        self._generate_script(model.trie, model.trie.root)

    def on_generate_selection_script(self, _):
        model = self.treeview.get_model()
        selected_node = self.treeview.get_selected_node()

        if selected_node is None:
            LOGGER.info('Nothing selected to make script from.')
            return

        self._generate_script(model.trie, selected_node)

    def on_default_action(self):
        """Called on Ctrl-Enter"""
        if self.actionbar.is_sensitive():
            self._generate_script(self.model)
