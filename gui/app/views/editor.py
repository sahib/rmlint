#!/usr/bin/env python
# encoding: utf-8

# Internal:
from app.util import View, IconButton
from app.runner import Script

# External:
from gi.repository import Gtk
from gi.repository import GLib
from gi.repository import Pango

# Fallback to the normal Gtk.TextView if no GtkSource.View could be imported
try:
    from gi.repository import GtkSource

    def _create_source_view():

        buffer_ = GtkSource.Buffer()
        buffer_.set_highlight_syntax(True)

        view = GtkSource.View()
        view.set_buffer(buffer_)
        view.set_show_line_numbers(True)
        view.set_show_line_marks(True)
        view.set_auto_indent(True)

        return view, buffer_

    def _set_source_style(view, style_name):
        style = GtkSource.StyleSchemeManager.get_default().get_scheme(
            style_name
        )

        if style:
            buffer_ = view.get_buffer()
            buffer_.set_style_scheme(style)

    def _set_source_lang(view, lang):
        language = GtkSource.LanguageManager.get_default().get_language(lang)
        buffer_ = view.get_buffer()
        buffer_.set_language(language)

except ImportError:
    def _create_source_view():
        buffer_ = Gtk.Buffer()
        view = Gtk.TextView()
        return view, buffer_

    def _set_source_style(view, style_name):
        pass

    def _set_source_lang(view, lang):
        pass




def _create_running_screen():
    grid = Gtk.Grid()
    spinner = Gtk.Spinner()
    spinner.start()
    spinner.set_halign(Gtk.Align.FILL)
    spinner.set_valign(Gtk.Align.FILL)
    spinner.set_vexpand(True)

    # grid.attach(spinner, 0, 0, 1, 1)

    label = Gtk.Label()
    label.get_style_context().add_class('dim-label')
    label.set_justify(Gtk.Justification.CENTER)
    label.set_markup('<span font="65">☕</span>')
    label.set_halign(Gtk.Align.CENTER)
    label.set_valign(Gtk.Align.CENTER)
    label.set_vexpand(True)

    grid.attach(label, 0, 1, 1, 1)
    return spinner


def _create_finished_screen():
    label = Gtk.Label()
    label.get_style_context().add_class('dim-label')
    label.set_justify(Gtk.Justification.CENTER)
    label.set_markup('<span font="65">✔</span>\n<big>All went well!</big>')
    return label


class EditorView(View):
    def __init__(self, win):
        View.__init__(self, win, sub_title='Step 3: Check the results')

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
        label.get_style_context().add_class('dim-label')
        label.set_justify(Gtk.Justification.CENTER)
        label.set_markup('''
<span font="65">☠</span>

<big><big><b>Review the script on the left!</b></big>
When done, click the `Run Script` button below.
</big>\n\n''')

        self.text_view, buffer_ = _create_source_view()
        self.text_view.set_name('ScriptEditor')
        self.text_view.set_vexpand(True)
        self.text_view.set_valign(Gtk.Align.FILL)
        self.text_view.set_hexpand(True)
        self.text_view.set_halign(Gtk.Align.FILL)

        buffer_.create_tag(
            "+", weight=Pango.Weight.BOLD, foreground="#00ff00"
        )
        buffer_.create_tag(
            "-", weight=Pango.Weight.BOLD, foreground="#ff0000"
        )

        _set_source_style(self.text_view, 'solarized-light')
        _set_source_lang(self.text_view, 'sh')

        try:
            with open('/tmp/rmlint.sh', 'r') as handle:
                buffer_.set_text(handle.read())
        except OSError:
            buffer_.set_text('echo "Place a rmlint.sh in /tmp/rmlint.sh"')

        scw = Gtk.ScrolledWindow()
        scw.add(self.text_view)

        separator = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        left_pane = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        left_pane.pack_start(scw, True , True, 0)
        left_pane.pack_start(separator, False, False, 0)

        self.run_script_btn = IconButton(
            'user-trash-symbolic', 'Run Script'
        )
        self.run_script_btn.set_halign(Gtk.Align.CENTER)
        self.run_script_btn.get_style_context().add_class(
            Gtk.STYLE_CLASS_DESTRUCTIVE_ACTION
        )
        self.run_script_btn.connect('clicked', self.on_run_script_clicked)

        control_grid.attach(label, 0, 0, 1, 1)
        control_grid.attach_next_to(
            self.run_script_btn, label, Gtk.PositionType.BOTTOM, 1, 1
        )
        control_grid.set_border_width(20)

        self.stack = Gtk.Stack()
        self.stack.set_transition_type(Gtk.StackTransitionType.SLIDE_UP)

        self.stack.add_named(control_grid, 'danger')
        self.stack.set_visible_child(control_grid)

        self.stack.add_named(_create_running_screen(), 'progressing')
        self.stack.add_named(_create_finished_screen(), 'finished')

        grid = Gtk.Grid()
        grid.attach(left_pane, 0, 0, 1, 1)
        grid.attach_next_to(self.stack, left_pane, Gtk.PositionType.RIGHT, 1, 1)
        self.add(grid)

    def on_run_script_clicked(self, button):
        self.sub_title = 'Step 4: Cross fingers'
        button.set_sensitive(False)
        self.text_view.set_sensitive(False)
        self.stack.set_visible_child_name('progressing')

        self.text_view.get_buffer().set_text('')
        _set_source_lang(self.text_view, '')

        def _line_read(script, prefix, line):
            buffer_ = self.text_view.get_buffer()

            prefix = prefix.lower()
            if prefix == 'keeping':
                tag, text = '+', '+ '
            elif prefix.startswith('Deleting'):
                tag, text = '-', '- '
            else:
                tag, text = '', prefix.capitalize()

            buffer_.insert(buffer_.get_end_iter(), '\n')

            if tag is not None:
                buffer_.insert_with_tags(buffer_.get_end_iter(), text, tag)

            buffer_.insert(buffer_.get_end_iter(), line, -1)

        def _script_finished(script):
            self.stack.set_visible_child_name('finished')

        script = Script('/tmp/rmlint.sh')
        script.connect('line-read', _line_read)
        script.connect('script-finished', _script_finished)
        script.run()

        # self.text_view.get_buffer().set_text('Deleting xyz (output of rmlint.sh)')

        # GLib.timeout_add(2000, lambda: self.stack.set_visible_child_name('finished'))


