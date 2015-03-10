#!/usr/bin/env python
# encoding: utf-8

# Internal:
from app.util import View, IconButton

# External:
from gi.repository import Gtk

# Fallback to the normal Gtk.TextView if no GtkSource.View could be imported
try:
    from gi.repository import GtkSource

    def _create_source_view():
        language = GtkSource.LanguageManager.get_default().get_language('sh')
        style= GtkSource.StyleSchemeManager.get_default().get_scheme(
            'solarized-light'
        )

        buffer_ = GtkSource.Buffer()
        buffer_.set_language(language)
        buffer_.set_highlight_syntax(True)
        buffer_.set_style_scheme(style)

        view = GtkSource.View()
        view.set_buffer(buffer_)
        view.set_show_line_numbers(True)
        view.set_show_line_marks(True)
        view.set_auto_indent(True)

        return view, buffer_
except ImportError:
    def _create_source_view():
        buffer_ = Gtk.Buffer()
        view = Gtk.TextView()
        return view, buffer_


class EditorView(View):
    def __init__(self, win):
        View.__init__(self, win)

        self.set_policy(
            Gtk.PolicyType.NEVER,
            Gtk.PolicyType.NEVER
        )

        control_grid = Gtk.Grid()
        control_grid.set_hexpand(False)
        control_grid.set_vexpand(False)
        control_grid.set_halign(Gtk.Align.CENTER)
        control_grid.set_valign(Gtk.Align.CENTER)

        label = Gtk.Label()
        label.set_justify(Gtk.Justification.CENTER)
        label.set_markup('''
<span font="65">☠</span>

<big>
<big><span color="darkgrey"><b>Review the script on the right!</b></span></big>
When done, click the `Run Script` button below.
</big>\n\n''')

        text_view, buffer_ = _create_source_view()
        text_view.set_name('ScriptEditor')
        text_view.set_vexpand(True)
        text_view.set_valign(Gtk.Align.FILL)

        with open('/tmp/rmlint.sh', 'r') as handle:
            buffer_.set_text(handle.read())

        scw = Gtk.ScrolledWindow()
        scw.add(text_view)

        separator = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        right_pane = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        right_pane.pack_start(separator, False, False, 0)
        right_pane.pack_start(scw, True , True, 0)

        run_script_btn = IconButton(
            'user-trash-symbolic', 'Run Script'
        )
        run_script_btn.set_halign(Gtk.Align.CENTER)
        run_script_btn.get_style_context().add_class(
            Gtk.STYLE_CLASS_DESTRUCTIVE_ACTION
        )
        control_grid.attach(label, 0, 0, 1, 1)
        control_grid.attach_next_to(
            run_script_btn, label, Gtk.PositionType.BOTTOM, 1, 1
        )

        grid = Gtk.Grid()
        grid.set_column_homogeneous(True)
        grid.attach(control_grid, 0, 0, 1, 1)
        grid.attach_next_to(right_pane, control_grid, Gtk.PositionType.RIGHT, 1, 1)
        self.add(grid)

    # def on_view_enter(self):
    #     self.app_window.show_action_buttons(
    #         None, 'Run script'
    #     )

    # def on_view_leave(self):
    #     self.app_window.hide_action_buttons()
