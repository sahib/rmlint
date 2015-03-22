#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import os
import stat
import tempfile

# Internal:
from app.util import View, IconButton, scrolled
from app.runner import Script

# External:
from gi.repository import Gtk
from gi.repository import GLib
from gi.repository import Pango
from gi.repository import GObject

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

# Fallback to the normal Gtk.TextView if no GtkSource.View could be imported
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
    spinner = Gtk.Spinner()
    spinner.start()
    return spinner


def _create_finished_screen(callback):
    control_grid = Gtk.Grid()
    control_grid.set_hexpand(False)
    control_grid.set_vexpand(False)
    control_grid.set_halign(Gtk.Align.CENTER)
    control_grid.set_valign(Gtk.Align.CENTER)

    label = Gtk.Label(
        use_markup=True,
        label='<span font="65">✔</span>\n\n\n<big><b>All went well!</b></big>\n\n\n\n\n',
        justify=Gtk.Justification.CENTER
    )
    label.get_style_context().add_class('dim-label')

    go_back = IconButton('go-jump-symbolic', 'Go back to Script')
    go_back.set_halign(Gtk.Align.CENTER)
    go_back.connect(
        'clicked', lambda _: callback()
    )

    control_grid.attach(label, 0, 0, 1, 1)
    control_grid.attach_next_to(
        go_back, label, Gtk.PositionType.BOTTOM, 1, 1
    )

    return control_grid


class ShredderDeletionEntry(Gtk.ListBoxRow):
    icon_plus = Gtk.IconTheme.get_default().load_icon('emblem-ok-symbolic', 24, 0)
    icon_minus = Gtk.IconTheme.get_default().load_icon('user-trash-full-symbolic', 24, 0)

    def __init__(self, path, is_original):
        Gtk.ListBoxRow.__init__(self)

        if is_original:
            icon = ShredderDeletionEntry.icon_plus
        else:
            icon = ShredderDeletionEntry.icon_minus


        image = Gtk.Image.new_from_pixbuf(icon)
        image.props.halign = Gtk.Align.END

        label = Gtk.Label(
            use_markup=True, justify=Gtk.Justification.LEFT
        )
        label.props.hexpand = True
        label.props.halign = Gtk.Align.START

        path = GLib.markup_escape_text(path)
        if is_original:
            label.set_markup('<b>{}</b>'.format(path))
        else:
            label.set_markup(path)

        grid = Gtk.Grid()
        grid.attach(label, 0, 0, 1, 1)
        grid.attach(image, 1, 0, 1, 1)
        self.add(grid)


class ShredderRunButton(Gtk.Box):
    dry_run = GObject.Property(type=bool, default=True)

    def __init__(self, icon, label):
        Gtk.Box.__init__(self)
        self.get_style_context().add_class(
            Gtk.STYLE_CLASS_LINKED
        )

        self.button = IconButton(icon, label)
        self.state = Gtk.ToggleButton()
        self.state.add(
            Gtk.Label(use_markup=True, label='<small>Dry run?</small>')
        )

        def _toggle_dry_run(btn):
            for widget in [self.button, self.state]:
                ctx = widget.get_style_context()
                if not btn.get_active():
                    ctx.remove_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)
                    ctx.add_class(Gtk.STYLE_CLASS_DESTRUCTIVE_ACTION)
                else:
                    ctx.remove_class(Gtk.STYLE_CLASS_DESTRUCTIVE_ACTION)
                    ctx.add_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)

        self.state.connect('toggled', _toggle_dry_run)

        self.pack_start(self.button, True, True, 0)
        self.pack_start(self.state, False, False, 0)
        self.bind_property(
            'dry_run', self.state, 'active',
            GObject.BindingFlags.BIDIRECTIONAL |
            GObject.BindingFlags.SYNC_CREATE
        )

        self.state.set_active(True)
        _toggle_dry_run(self.state)


def _create_icon_stack():
    icon_stack = Gtk.Stack()
    icon_stack.set_transition_type(
        Gtk.StackTransitionType.SLIDE_LEFT_RIGHT
    )

    for name, symbol in (('warning', '⚠'), ('danger', '☠')):
        icon_label = Gtk.Label(
            use_markup=True,
            justify=Gtk.Justification.CENTER
        )
        icon_label.get_style_context().add_class(
            Gtk.STYLE_CLASS_DIM_LABEL
        )
        icon_label.set_markup(
            '<span font="65">{symbol}</span>'.format(symbol=symbol)
        )
        icon_stack.add_named(icon_label, name)

    return icon_stack


class EditorView(View):
    def __init__(self, win):
        View.__init__(self, win)
        self.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.NEVER)

        control_grid = Gtk.Grid()
        control_grid.set_hexpand(False)
        control_grid.set_vexpand(False)
        control_grid.set_halign(Gtk.Align.CENTER)
        control_grid.set_valign(Gtk.Align.CENTER)

        label = Gtk.Label(
            use_markup=True,
            justify=Gtk.Justification.CENTER
        )
        label.get_style_context().add_class(
            Gtk.STYLE_CLASS_DIM_LABEL
        )
        label.set_markup('''

<big><b>Review the script on the left!</b></big>
When done, click the `Run Script` button below.
\n\n'''
        )

        icon_stack = _create_icon_stack()

        self.text_view, buffer_ = _create_source_view()
        self.text_view.set_name('ScriptEditor')
        self.text_view.set_vexpand(True)
        self.text_view.set_valign(Gtk.Align.FILL)
        self.text_view.set_hexpand(True)
        self.text_view.set_halign(Gtk.Align.FILL)

        buffer_.create_tag("original", weight=Pango.Weight.BOLD)
        buffer_.create_tag("normal")

        icon_theme = Gtk.IconTheme.get_default()

        self.left_listbox = Gtk.ListBox()
        self.left_listbox.set_selection_mode(Gtk.SelectionMode.NONE)

        self.left_stack = Gtk.Stack()
        self.left_stack.set_transition_type(
            Gtk.StackTransitionType.OVER_RIGHT_LEFT
        )

        self.left_stack.add_named(scrolled(self.text_view), 'script')
        self.left_stack.add_named(scrolled(self.left_listbox), 'list')

        separator = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        left_pane = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        left_pane.pack_start(self.left_stack, True , True, 0)
        left_pane.pack_start(separator, False, False, 0)

        run_button = ShredderRunButton(
            'user-trash-symbolic', 'Run Script'
        )
        run_button.button.connect('clicked', self.on_run_script_clicked)
        run_button.set_halign(Gtk.Align.CENTER)
        run_button.connect(
            'notify::dry-run',
            lambda btn, _: icon_stack.set_visible_child_name(
                'warning' if btn.dry_run else 'danger'
            )
        )

        control_grid.attach(label, 0, 0, 1, 1)
        control_grid.attach_next_to(
            run_button, label, Gtk.PositionType.BOTTOM, 1, 1
        )
        control_grid.attach_next_to(
            icon_stack, label, Gtk.PositionType.TOP, 1, 1
        )
        control_grid.set_border_width(15)

        self.stack = Gtk.Stack()
        self.stack.set_transition_type(Gtk.StackTransitionType.SLIDE_UP)

        self.stack.add_named(control_grid, 'danger')
        self.stack.add_named(_create_running_screen(), 'progressing')
        self.stack.add_named(
            _create_finished_screen(self.switch_to_script), 'finished'
        )

        self.switch_to_script()

        grid = Gtk.Grid()
        grid.attach(left_pane, 0, 0, 1, 1)
        grid.attach_next_to(self.stack, left_pane, Gtk.PositionType.RIGHT, 1, 1)
        self.add(grid)

    def switch_to_script(self):
        self.sub_title = 'Step 3: Check the results'
        self.left_stack.set_visible_child_name('script')
        buffer_ = self.text_view.get_buffer()
        try:
            with open('/tmp/rmlint.sh', 'r') as handle:
                buffer_.set_text(handle.read())
        except OSError:
            buffer_.set_text('echo "Place a rmlint.sh in /tmp/rmlint.sh"')

        _set_source_style(self.text_view, 'solarized-light')
        _set_source_lang(self.text_view, 'sh')

        self.stack.set_visible_child_name('danger')

    def on_run_script_clicked(self, button):
        self.sub_title = 'Step 4: Cross fingers!'
        self.stack.set_visible_child_name('progressing')
        self.left_stack.set_visible_child_name('list')

        for row in self.left_listbox:
            self.left_listbox.remove(row)

        def _line_read(script, prefix, line):
            entry = ShredderDeletionEntry(
                prefix + ': ' + line, (prefix.lower() == 'keeping')
            )

            self.left_listbox.insert(entry, -1)
            self.left_listbox.show_all()

        def _script_finished(script):
            self.stack.set_visible_child_name('finished')

        temp_path = tempfile.mktemp(suffix='-rmlint.sh', prefix='Shredder-')

        with open(temp_path, 'w') as handle:
            buffer_ = self.text_view.get_buffer()
            handle.write(
                buffer_.get_text(
                    buffer_.get_start_iter(),
                    buffer_.get_end_iter(),
                    False
                )
            )
        os.chmod(temp_path, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)

        script = Script(temp_path)
        script.connect('line-read', _line_read)
        script.connect('script-finished', _script_finished)
        script.run()
