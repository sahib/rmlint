#!/usr/bin/env python
# encoding: utf-8

"""
Editor view.

Provides means to view the generated script and edit it.
Also contains the dangerous run button that triggers the final run.
Once clicked, a counter will be shown, showing the total number
of killed files in terms of size.
"""


# Stdlib:
import os
import time
import logging

# External:
from gi.repository import Gtk
from gi.repository import GLib
from gi.repository import Pango
from gi.repository import Polkit
from gi.repository import GObject

# Internal:
from shredder.util import View, IconButton, scrolled, size_to_human_readable
from shredder.util import MultipleChoiceButton, SuggestedButton
from shredder.runner import Script


LOGGER = logging.getLogger('editor')


REMOVED_LABEL = '''<big>{s}</big><small> removed</small>
<small>{t}</small> <b><big>{p}</big></b>
'''


#############################
# Try to load GtkSourceView #
#############################

try:
    from gi.repository import GtkSource

    def _create_source_view():
        """Create a suitable text view + buffer for showing a sh script."""
        LOGGER.info('Using GtkSourceView since we have it.')

        buffer_ = GtkSource.Buffer()
        buffer_.set_highlight_syntax(True)

        view = GtkSource.View()
        view.set_buffer(buffer_)
        view.set_show_line_numbers(True)
        view.set_show_line_marks(True)
        view.set_auto_indent(True)

        return view, buffer_

    class _SearchRun:
        """Represent a run through all matches relative a query."""
        def __init__(self, buffer_, query):
            settings = GtkSource.SearchSettings()
            settings.set_search_text(query)
            settings.set_case_sensitive(False)

            self.ctx = GtkSource.SearchContext.new(buffer_, settings)
            self.ctx.set_highlight(True)
            self.ctx.set_match_style(GtkSource.Style(underline=True))
            self._last_mark = None

        @property
        def query(self):
            """Query of this search run"""
            return self.ctx.get_settings().get_search_text()

        def next_hop(self, view):
            """Return a GtkTextMark pointing at the next find or None.

            All matching items will be highlighted by default.
            """
            # Find out at which position we were last:
            buffer_ = self.ctx.props.buffer
            if self._last_mark is None:
                offset = buffer_.get_start_iter()
            else:
                offset = buffer_.get_iter_at_mark(self._last_mark)

            # Trigger actual searching:
            self.ctx.forward_async(offset, None, self.on_forward_finish, view)

        def on_forward_finish(self, source, result, view):
            """Called once GtkSourceSearchContext has finished processing."""
            valid, start, end = self.ctx.forward_finish(result)
            self._last_mark = None

            # Select & remember mark for next hop
            if valid:
                buffer_ = self.ctx.props.buffer
                buffer_.select_range(start, end)
                self._last_mark = buffer_.create_mark(None, end, True)
                view.scroll_mark_onscreen(self._last_mark)

    def _set_source_style(view, style_name):
        """If supported, set a color scheme by name."""
        style = GtkSource.StyleSchemeManager.get_default().get_scheme(
            style_name
        )

        if style:
            buffer_ = view.get_buffer()
            buffer_.set_style_scheme(style)

    def _set_source_lang(view, lang):
        """If supported, set a syntax highlighter to use."""
        language = GtkSource.LanguageManager.get_default().get_language(lang)
        buffer_ = view.get_buffer()
        buffer_.set_language(language)

# Fallback to the normal Gtk.TextView if no GtkSource.View could be imported
# This is the bare minimum we support. It's neither pretty nor very useful.
except ImportError:

    def _create_source_view():
        """Create a suitable text view + buffer for showing a sh script."""
        LOGGER.info('No GtkSourceView found.')

        buffer_ = Gtk.TextBuffer()
        view = Gtk.TextView()
        return view, buffer_

    class _SearchRun:
        """Dummy search functor that does nothing."""
        def __init__(self, *_):
            self.query = None

        def next_hop(self, *_):
            """No-op"""
            pass

    def _set_source_style(*_):
        """If supported, set a color scheme by name."""
        pass  # Not supported.

    def _set_source_lang(*_):
        """If supported, set a syntax highlighter to use."""
        pass  # Not supported.


def _create_running_screen():
    """Helper to configure a spinner for the delete screen."""
    spinner = Gtk.Spinner()
    spinner.start()
    return spinner


def _create_finished_screen(callback):
    """Give the user a nice, warm feeling."""
    control_grid = Gtk.Grid()
    control_grid.set_hexpand(False)
    control_grid.set_vexpand(False)
    control_grid.set_halign(Gtk.Align.CENTER)
    control_grid.set_valign(Gtk.Align.CENTER)

    # Lies make the user feel comfortable:
    label = Gtk.Label(
        use_markup=True,
        label='''<span font="65">✔</span>


<big><b>All went well!</b></big>




''',
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


class RunningLabel(Gtk.Label):
    """Centered large label showing a size sum and the current deleted path."""
    def __init__(self):
        Gtk.Label.__init__(self)

        # Basename is more important:
        self.set_ellipsize(Pango.EllipsizeMode.START)

        # Make it appeared a bit dimmed:
        self.get_style_context().add_class(
            Gtk.STYLE_CLASS_DIM_LABEL
        )
        self.set_justify(Gtk.Justification.CENTER)
        self.reset()

    def push(self, prefix, path):
        """Push a new path to the label, removing the old one."""
        if prefix.lower() == 'keeping':
            return

        try:
            buf = os.stat(path)
            self._size_sum += buf.st_size
        except OSError:
            pass

        text = REMOVED_LABEL.format(
            t=prefix,
            s=size_to_human_readable(self._size_sum),
            p=GLib.markup_escape_text(path)
        )
        self.set_markup(text)

    def reset(self):
        """Reset the counter to initial state (zero)"""
        self._size_sum = 0
        self.push('', '')


class RunButton(Gtk.Box):
    """Customized run button that can change color."""
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

        self.state.connect('toggled', self._toggle_dry_run)

        self.pack_start(self.button, True, True, 0)
        self.pack_start(self.state, False, False, 0)
        self.bind_property(
            'dry_run', self.state, 'active',
            GObject.BindingFlags.BIDIRECTIONAL |
            GObject.BindingFlags.SYNC_CREATE
        )

        self.state.set_active(True)
        self._toggle_dry_run(self.state)

    def set_sensitive(self, mode):
        btn_ctx = self.button.get_style_context()
        dry_ctx = self.state.get_style_context()

        if mode:
            btn_ctx.add_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)
            dry_ctx.add_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)
        else:
            btn_ctx.remove_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)
            dry_ctx.remove_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)

        self.button.set_sensitive(mode)
        self.state.set_sensitive(mode)

    def _toggle_dry_run(self, btn):
        """Change the color and severeness of the button."""
        for widget in [self.button, self.state]:
            ctx = widget.get_style_context()
            if not btn.get_active():
                ctx.remove_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)
                ctx.add_class(Gtk.STYLE_CLASS_DESTRUCTIVE_ACTION)
            else:
                ctx.remove_class(Gtk.STYLE_CLASS_DESTRUCTIVE_ACTION)
                ctx.add_class(Gtk.STYLE_CLASS_SUGGESTED_ACTION)


def _create_icon_stack():
    """Create a small widget that shows alternating icons."""
    icon_stack = Gtk.Stack()
    icon_stack.set_transition_type(
        Gtk.StackTransitionType.CROSSFADE
    )
    icon_stack.set_transition_duration(100)

    for name, symbol in (('warning', '⚠'), ('danger', '☠'), ('info', 'ℹ')):
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


class ScriptSaverDialog(Gtk.FileChooserWidget):
    """GtkFileChooserWidget tailored for saving a `Script` instance."""

    __gsignals__ = {
        'saved': (GObject.SIGNAL_RUN_FIRST, None, ()),
    }

    def __init__(self, editor_view):
        Gtk.FileChooserWidget.__init__(self)

        self.editor_view = editor_view
        self.set_select_multiple(False)
        self.set_create_folders(False)
        self.set_action(Gtk.FileChooserAction.SAVE)
        self.set_do_overwrite_confirmation(True)

        self.file_type = MultipleChoiceButton(
            ['sh', 'json', 'csv'], 'sh', 'sh'
        )
        self.file_type.set_halign(Gtk.Align.START)
        self.file_type.set_hexpand(True)
        self.file_type.connect('row-selected', self.on_file_type_changed)
        self.file_type.props.margin_end = 10

        self.confirm = SuggestedButton('Save')
        self.confirm.connect('clicked', self.on_save_clicked)
        self.confirm.set_halign(Gtk.Align.END)
        self.confirm.set_hexpand(False)
        self.confirm.set_sensitive(False)
        self.confirm.props.margin_end = 10

        self.cancel_button = IconButton('window-close-symbolic', 'Cancel')
        self.cancel_button.connect('clicked', self.on_cancel_clicked)
        self.cancel_button.set_halign(Gtk.Align.END)
        self.cancel_button.set_hexpand(False)

        self.connect('selection-changed', self.on_selection_changed)

        file_type_label = Gtk.Label('<b>Filetype</b>')
        file_type_label.set_use_markup(True)
        file_type_label.props.margin_end = 5
        file_type_label.get_style_context().add_class(
            Gtk.STYLE_CLASS_DIM_LABEL
        )

        self.extra_box = Gtk.Grid()
        self.extra_box.attach(self.file_type, 0, 0, 1, 1)
        self.extra_box.attach(self.confirm, 1, 0, 1, 1)
        self.extra_box.attach_next_to(
            file_type_label,
            self.file_type,
            Gtk.PositionType.LEFT,
            1,
            1
        )

        self.extra_box.set_hexpand(True)
        self.extra_box.set_halign(Gtk.Align.FILL)

    def show_controls(self):
        """Show cancel, save and file type chooser buttons."""
        self.editor_view.add_header_widget(self.extra_box)
        self.editor_view.add_header_widget(
            self.cancel_button, align=Gtk.Align.START
        )

        self.update_file_suggestion()

    def update_file_suggestion(self):
        """Suggest a name for the script to save."""
        file_type = self.file_type.get_selected_choice() or 'sh'
        self.set_current_name(time.strftime('rmlint-%FT%T%z.' + file_type))

    def on_file_type_changed(self, _):
        """Called once the user chose a different format"""
        current_path = self.get_filename()
        if not current_path:
            self.update_file_suggestion()
        else:
            try:
                path, _ = current_path.rsplit('.', 1)
                self.set_current_name(
                    path + '.' + self.file_type.get_selected_choice() or ''
                )
            except ValueError:
                # No extension. Leave it.
                pass

    def _exit_from_save(self):
        """Preparation to go back to script view."""
        self.emit('saved')
        self.editor_view.clear_header_widgets()

    def on_cancel_clicked(self, _):
        """Signal handler for the cancel button."""
        self._exit_from_save()

    def on_save_clicked(self, _):
        """Called once the user clicked the `Save` button"""
        file_type = self.file_type.get_selected_choice()
        abs_path = self.get_filename()

        runner = self.editor_view.app_window.views['runner'].runner
        LOGGER.info('Saving script as `%s` to: %s', file_type, abs_path)
        runner.save(abs_path, file_type)
        self._exit_from_save()

    def on_selection_changed(self, _):
        """Called once a file or directory was clicked"""
        filename = self.get_filename()
        self.confirm.set_sensitive(bool(filename))

        # Make sure the user-typed extension gets set in teh type chooser also.
        name = self.get_current_name()
        *_, extension = name.rsplit('.', 1)
        self.file_type.set_selected_choice(extension)


class OverlaySaveButton(Gtk.Overlay):
    """Button box that contains two buttons in a overlay.
    The overlay is shown on top of the script editor.
    Buttons are: A unlock button for asking for root permissions
    and a save button to save the script somewhere.
    """

    __gsignals__ = {
        'save-clicked': (GObject.SIGNAL_RUN_FIRST, None, ()),
        'unlock-clicked': (GObject.SIGNAL_RUN_FIRST, None, ())
    }

    def __init__(self):
        Gtk.Overlay.__init__(self)

        self._box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        self._box.get_style_context().add_class(
            Gtk.STYLE_CLASS_LINKED
        )

        try:
            perm = Polkit.Permission.new_sync(
                'org.freedesktop.accounts.user-administration',
                Polkit.UnixProcess.new_for_owner(os.getpid(), 0, -1),
                None
            )
            self._lock_button.set_permission(perm)
        except GLib.Error as err:
            LOGGER.warning('Unable to get polkit permissions: ' + str(err))

        self._lock_button = Gtk.LockButton()
        self._lock_button.props.margin = 20
        self._lock_button.props.margin_end = 0
        self._lock_button.connect(
            'clicked', lambda _: self.emit('unlock-clicked')
        )

        self._save_button = IconButton(
            'folder-download-symbolic', 'Save to file'
        )
        self._save_button.props.margin = 20
        self._save_button.props.margin_start = 0
        self._save_button.connect(
            'clicked', lambda _: self.emit('save-clicked')
        )

        self._box.pack_start(self._lock_button, False, True, 0)
        self._box.pack_start(self._save_button, False, True, 0)
        self._box.set_hexpand(False)
        self._box.set_vexpand(False)
        self._box.set_halign(Gtk.Align.END)
        self._box.set_valign(Gtk.Align.END)
        self.add_overlay(self._box)


class EditorView(View):
    """Actual view class."""
    def __init__(self, win):
        View.__init__(self, win)

        self._last_runner = None
        self.script = Script.create_dummy()

        control_grid = Gtk.Grid()
        control_grid.set_hexpand(False)
        control_grid.set_vexpand(False)
        control_grid.set_halign(Gtk.Align.CENTER)
        control_grid.set_valign(Gtk.Align.CENTER)

        self.info_label = Gtk.Label(
            use_markup=True,
            justify=Gtk.Justification.CENTER
        )
        self.info_label.get_style_context().add_class(
            Gtk.STYLE_CLASS_DIM_LABEL
        )
        self.set_info_review_text()

        self.icon_stack = _create_icon_stack()

        self.text_view, buffer_ = _create_source_view()
        self.text_view.set_name('ShredderScriptEditor')
        self.text_view.set_vexpand(True)
        self.text_view.set_valign(Gtk.Align.FILL)
        self.text_view.set_hexpand(True)
        self.text_view.set_halign(Gtk.Align.FILL)
        self.save_button = OverlaySaveButton()
        self.save_button.add(scrolled(self.text_view))
        self.save_chooser = ScriptSaverDialog(self)

        def on_save_button_clicked(_):
            """Switch to the save dialog in the stack."""
            self.set_search_mode(False)
            self.left_stack.set_visible_child_name('chooser')
            self.save_chooser.show_controls()
            self.set_info_help_text()
            self.set_correct_icon()
            self.run_button.set_sensitive(False)

        def on_save_clicked(_):
            """Switch back when the user has saved."""
            self.left_stack.set_visible_child_name('script')
            self.set_info_review_text()
            self.set_correct_icon()
            self.run_button.set_sensitive(True)

        self.save_button.connect(
            'save-clicked', on_save_button_clicked
        )

        self.save_chooser.connect(
            'saved', on_save_clicked
        )

        buffer_.create_tag("original", weight=Pango.Weight.BOLD)
        buffer_.create_tag("normal")

        self.run_label = RunningLabel()
        self.run_label.set_hexpand(False)
        self.run_label.set_halign(Gtk.Align.FILL)

        self.left_stack = Gtk.Stack()
        self.left_stack.set_transition_type(
            Gtk.StackTransitionType.SLIDE_UP
        )

        spinner = Gtk.Spinner()
        spinner.start()

        self.left_stack.add_named(spinner, 'loading')
        self.left_stack.add_named(self.save_button, 'script')
        self.left_stack.add_named(self.save_chooser, 'chooser')
        self.left_stack.add_named(scrolled(self.run_label), 'list')

        separator = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        left_pane = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        left_pane.pack_start(self.left_stack, True, True, 0)
        left_pane.pack_start(separator, False, False, 0)

        self.run_button = RunButton(
            'user-trash-symbolic', 'Run Script'
        )
        self.run_button.button.connect('clicked', self.on_run_script_clicked)
        self.run_button.set_halign(Gtk.Align.CENTER)
        self.run_button.connect(
            'notify::dry-run', lambda *_: self.set_correct_icon()
        )

        control_grid.attach(self.info_label, 0, 0, 1, 1)
        control_grid.attach_next_to(
            self.run_button, self.info_label, Gtk.PositionType.BOTTOM, 1, 1
        )
        control_grid.attach_next_to(
            self.icon_stack, self.info_label, Gtk.PositionType.TOP, 1, 1
        )
        control_grid.set_border_width(15)

        self.stack = Gtk.Stack()
        self.stack.set_transition_type(Gtk.StackTransitionType.SLIDE_UP)

        self.stack.add_named(control_grid, 'danger')
        self.stack.add_named(_create_running_screen(), 'progressing')

        self.stack.add_named(
            _create_finished_screen(self._switch_back), 'finished'
        )

        self.left_stack.set_visible_child_name('script')

        paned = Gtk.Paned()
        paned.pack1(left_pane, True, True)
        paned.pack2(self.stack, True, True)
        paned.props.position = 920
        self.add(paned)

        self._last_search = None
        self.search_entry.connect(
            'search-changed', self.on_search_changed
        )
        self.search_entry.connect(
            'next-match', self.on_search_changed
        )

    def set_correct_icon(self):
        """Set the correct icon of icon_stack (either warning, skull or info)"""
        icon_name = 'info'

        if self.left_stack.get_visible_child_name() == 'script':
            if self.run_button.dry_run:
                icon_name = 'warning'
            else:
                icon_name = 'danger'

        self.icon_stack.set_visible_child_name(icon_name)

    def set_info_review_text(self):
        """Set the normal 'Review the script' text."""
        self.info_label.set_markup('''

<big><b>Review the script on the left!</b></big>
When done, click the `Run Script` button below.
\n\n''')

    def set_info_help_text(self):
        """Be a bit more helpful on the help dialog."""
        self.info_label.set_markup('''
<big><b>Save the script for later!</b></big>

It can be executed via <span font_family="monospace">./rmlint.sh</span>
Or you can replay the output later with:
    <span font_family="monospace">rmlint --replay /path/to/file.json</span>
''')

    def _switch_back(self):
        """Switch back from delete-view to script view"""
        self.switch_to_script()

    def switch_to_script(self):
        """Read and show the script."""
        self.sub_title = 'Check the results'
        GLib.idle_add(
            lambda: self.left_stack.set_visible_child_name('script')
        )
        buffer_ = self.text_view.get_buffer()
        buffer_.set_text(self.script.read())

        # Make sure it gets colored again:
        _set_source_style(self.text_view, 'solarized-light')
        _set_source_lang(self.text_view, 'sh')
        self.stack.set_visible_child_name('danger')
        self.stack.set_sensitive(True)

    def on_search_changed(self, _):
        """Called once the user enteres a new search query."""
        query = self.search_entry.get_text().lower()
        buffer_ = self.text_view.get_buffer()

        # If query is empty, just deselect everything.
        if not query:
            buffer_.select_range(
                buffer_.get_start_iter(),
                buffer_.get_start_iter()
            )

        # Check if query changed and if we need to get a new search ctx
        if self._last_search is None or self._last_search.query != query:
            self._last_search = _SearchRun(buffer_, query)

        # Jump one position ahead (or do initial search)
        self._last_search.next_hop(self.text_view)

    def on_run_script_clicked(self, _):
        """The critical function callback that is run when action is done."""
        self.set_search_mode(False)
        self.sub_title = 'Shreddering. Cross fingers!'
        self.stack.set_visible_child_name('progressing')
        self.left_stack.set_visible_child_name('list')

        LOGGER.info('Running script.')
        self.run_label.reset()
        self.script.run(dry_run=self.run_button.dry_run)

    def on_view_enter(self):
        """Called once the view becomes visible."""
        self.run_button.set_sensitive(True)

        # Re-read the script.
        runner = self.app_window.views['runner'].runner
        if runner is not None and self._last_runner is not runner:
            runner.connect('replay-finished', self.on_replay_finish, runner)
            self._last_runner = runner
            self.left_stack.set_visible_child_name('loading')
            self.stack.set_sensitive(False)

    def on_replay_finish(self, _, runner):
        """Called once ``rmlint --replay`` finished running."""
        LOGGER.info('Loading script from temporary directory')
        self.override_script(Script(runner.get_sh_path()))

    def on_default_action(self):
        """Called on Ctrl-Enter"""
        visible_screen = self.stack.get_visible_child_name()
        if visible_screen == 'danger':
            self.on_run_script_clicked(None)
        elif visible_screen == 'finished':
            self.switch_to_script()

    def override_script(self, script):
        """This method is for testing and cmdline use only."""
        LOGGER.info('Loading developer-defined script.')
        self.script = script

        self.script.connect(
            'line-read',
            lambda _, prefix, line: self.run_label.push(prefix, line)
        )

        self.script.connect(
            'script-finished',
            lambda *_: self.stack.set_visible_child_name('finished')
        )
        self.switch_to_script()
