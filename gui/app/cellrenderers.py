#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
from datetime import datetime

# Internal:
from app.util import IndicatorLabel

# External:
from gi.repository import Gtk
from gi.repository import GObject


class CellRendererSize(Gtk.CellRendererText):
    size = GObject.Property(type=int, default=0)

    def __init__(self, **kwargs):
        Gtk.CellRendererText.__init__(self, **kwargs)
        self.connect('notify::size', CellRendererSize._transform_size)

    def _transform_size(self, _):
        size = self.get_property('size')
        human_readable = ''

        if size > 0:
            for unit in ['', 'k', 'm', 'g', 't', 'p', 'e', 'z']:
                if abs(size) >= 1024.0:
                    size /= 1024.0
                    continue

                # Hack to split off the unneeded .0
                size = round(size, 1) if size % 1 else int(size)
                human_readable = "{s} {f}B".format(s=size, f=unit)
                break

        self.set_property('text', human_readable)


def pretty_date(time=False):
    """Get a datetime object or a int() Epoch timestamp and return a
    pretty string like 'an hour ago', 'Yesterday', '3 months ago',
    'just now', etc
    """
    diff = datetime.now() - time
    second_diff = diff.seconds
    day_diff = diff.days

    if day_diff < 0:
        return ''

    rnd = lambda t: '{}'.format(round(t, 1) if t % 1 else int(t))

    if day_diff == 0:
        if second_diff < 10:
            return "just now"
        if second_diff < 60:
            return rnd(second_diff) + " seconds ago"
        if second_diff < 120:
            return "a minute ago"
        if second_diff < 3600:
            return rnd(second_diff / 60) + " minutes ago"
        if second_diff < 7200:
            return "an hour ago"
        if second_diff < 86400:
            return rnd(second_diff / 3600) + " hours ago"
    if day_diff == 1:
        return "Yesterday"
    if day_diff < 7:
        return rnd(day_diff) + " days ago"
    if day_diff < 31:
        return rnd(day_diff / 7) + " weeks ago"
    if day_diff < 365:
        return rnd(day_diff / 30) + " months ago"
    return rnd(day_diff / 365) + " years ago"


class CellRendererModifiedTime(Gtk.CellRendererText):
    mtime = GObject.Property(type=int, default=0)

    def __init__(self, **kwargs):
        Gtk.CellRendererText.__init__(self, **kwargs)
        self.connect(
            'notify::mtime',
            CellRendererModifiedTime._transform_mtime
        )

    def _transform_mtime(self, _):
        mtime = self.get_property('mtime')
        if mtime <= 0:
            pretty_date_str = ''
        else:
            pretty_date_str = pretty_date(time=datetime.fromtimestamp(mtime))

        self.set_property('text', pretty_date_str)


class CellRendererCount(Gtk.CellRendererText):
    count = GObject.Property(type=int, default=-1)

    def __init__(self, **kwargs):
        Gtk.CellRendererText.__init__(self, **kwargs)
        self.connect(
            'notify::count',
            CellRendererCount._transform_count
        )

    def _transform_count(self, _):
        count = self.get_property('count')
        is_plural = abs(count) is not 1

        if count > 0:
            name = 'Objects' if is_plural else 'Object'
            text = '{} {}'.format(count, name)
        elif count < 0:
            name = 'Twins' if is_plural else 'Twin'
            text = '{} {}'.format(-count, name)
        else:
            text = ''

        self.set_property('text', text)


# TODO: This is a util func
def _render_pixbuf(widget, width, height):
    # Use an OffscreenWindow to render the widget
    off_win = Gtk.OffscreenWindow()
    off_win.add(widget)
    off_win.set_size_request(width, height)
    off_win.show_all()

    # this is needed, otherwise the screenshot is black
    while Gtk.events_pending():
        Gtk.main_iteration()

    # Render the widget to a GdkPixbuf
    widget_pix = off_win.get_pixbuf()
    return widget_pix




def _render_tag_label(tag):
    state_to_symbol = {
        IndicatorLabel.NONE: '',
        IndicatorLabel.SUCCESS: '✔',
        IndicatorLabel.ERROR: '✗',
        IndicatorLabel.THEME: '♔'
    }

    tag_label = IndicatorLabel('<small>{symbol}</small>'.format(
        symbol=state_to_symbol[tag]
    ))
    tag_label.set_state(tag)

    # Render the label tag
    return _render_pixbuf(tag_label, -1, -1)


class CellRendererLint(Gtk.CellRendererText):
    ICON_SIZE = 20
    STATE_TO_PIXBUF = {}

    tag = GObject.Property(type=int, default=IndicatorLabel.ERROR)

    def __init__(self, **kwargs):
        Gtk.CellRendererText.__init__(self, **kwargs)
        self._pixbuf_renderer = Gtk.CellRendererPixbuf()
        self._pixbuf_renderer.set_alignment(0.0, 0.6)

    def do_render(self, ctx, widget, background_area, cell_area, flags):
        tag = self.get_property('tag')
        if tag is not IndicatorLabel.NONE:
            # Render the label tag
            pixbuf = CellRendererLint.STATE_TO_PIXBUF.get(tag)
            if pixbuf is None:
                pixbuf = CellRendererLint.STATE_TO_PIXBUF[tag] = _render_tag_label(tag)

            self._pixbuf_renderer.set_property('pixbuf', pixbuf)
            self._pixbuf_renderer.render(
                ctx, widget, background_area, cell_area, flags
            )

            # Render the text by calling super. Tell it where to render first.
            w = self._pixbuf_renderer.get_size(widget, cell_area)[2] + 3
            cell_area.x += w
            cell_area.width -= w
            background_area.x += w
            background_area.width -= w

        Gtk.CellRendererText.do_render(
            self, ctx, widget, background_area, cell_area, flags
        )

    def do_get_size(self, widget, cell_area):
        x, y, w, h = Gtk.CellRendererText.do_get_size()

        tag = self.get_property('tag')
        if tag is not IndicatorLabel.NONE:
            x += CellRendererText.ICON_SIZE
            w += CellRendererText.ICON_SIZE

        return x, y, w, h
