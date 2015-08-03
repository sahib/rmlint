#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
from datetime import datetime

# Internal:
from shredder.util import IndicatorLabel, render_pixbuf, size_to_human_readable

# External:
from gi.repository import Gtk
from gi.repository import GObject


class CellRendererSize(Gtk.CellRendererText):
    size = GObject.Property(type=int, default=0)

    def __init__(self, **kwargs):
        Gtk.CellRendererText.__init__(self, **kwargs)
        self.connect('notify::size', CellRendererSize._transform_size)

    def _transform_size(self, _):
        self.set_property(
            'text', size_to_human_readable(self.get_property('size'))
        )


def _rnd(num):
    """Round to minimal decimal places & convert to str"""
    return str(round(num, 1) if num % 1 else int(num))


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

    if day_diff == 0:
        if second_diff < 10:
            return "just now"
        elif second_diff < 60:
            return _rnd(second_diff) + " seconds ago"
        elif second_diff < 120:
            return "a minute ago"
        elif second_diff < 3600:
            return _rnd(second_diff / 60) + " minutes ago"
        elif second_diff < 7200:
            return "an hour ago"
        elif second_diff < 86400:
            return _rnd(second_diff / 3600) + " hours ago"
    elif day_diff == 1:
        return "Yesterday"
    elif day_diff < 7:
        return _rnd(day_diff) + " days ago"
    elif day_diff < 31:
        return _rnd(day_diff / 7) + " weeks ago"
    elif day_diff < 365:
        return _rnd(day_diff / 30) + " months ago"

    return _rnd(day_diff / 365) + " years ago"


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


def _render_tag_label(tag):
    state_to_symbol = {
        IndicatorLabel.NONE: '',
        IndicatorLabel.SUCCESS: '<span color="green">✔</span>',
        IndicatorLabel.WARNING: '<span color="orange">⚠</span>',
        IndicatorLabel.ERROR: '✗',
        IndicatorLabel.THEME: '<span color="blue">♔</span>'
    }

    tag_label = Gtk.Label('')
    tag_label.set_markup('<small>{symbol}</small>'.format(
        symbol=state_to_symbol[tag]
    ))
    # tag_label.set_state(tag)

    # Render the label tag
    return render_pixbuf(tag_label, -1, -1)


class CellRendererLint(Gtk.CellRendererPixbuf):
    ICON_SIZE = 20
    STATE_TO_PIXBUF = {}

    tag = GObject.Property(type=int, default=IndicatorLabel.ERROR)

    def __init__(self, **kwargs):
        Gtk.CellRendererPixbuf.__init__(self, **kwargs)
        self.set_alignment(0.0, 0.6)

    def do_render(self, ctx, widget, background_area, cell_area, flags):
        tag = self.get_property('tag')
        if tag is IndicatorLabel.NONE:
            return

        # Render the label tag and cache it if necessary
        lookup_table = CellRendererLint.STATE_TO_PIXBUF
        pixbuf = lookup_table.get(tag)
        if pixbuf is None:
            pixbuf = lookup_table[tag] = _render_tag_label(tag)

        # Actual rendering
        self.set_property('pixbuf', pixbuf)
        ctx.set_source_rgb(0, 255, 0)
        Gtk.CellRendererPixbuf.do_render(
            self, ctx, widget, background_area, cell_area, flags
        )

    def do_get_size(self, widget, cell_area):
        w, h = [self.props.xpad * 2 + CellRendererLint.ICON_SIZE] * 2

        x, y = 0, 0
        if cell_area:
            x = max(0, self.props.xalign * (cell_area.width - w))
            y = max(0, self.props.yalign * (cell_area.height - h))

        return x, y, w, h
