#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import math
import colorsys

# External:
import cairo

from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import GLib
from gi.repository import GObject

from gi.repository import Pango
from gi.repository import PangoCairo


def _draw_center_text(ctx, alloc, text, font_size=10):
    '''Draw a text at the center of ctx/alloc.

    ctx: a cairo Context to draw to
    alloc: size of area.
    text: Actual text as unicode string
    font_size: Size of the font to take
    '''
    layout = PangoCairo.create_layout(ctx)
    font = Pango.FontDescription.from_string('Ubuntu Light')
    font.set_size(font_size * Pango.SCALE)
    layout.set_font_description(font)
    layout.set_markup(text, -1)
    layout.set_alignment(Pango.Alignment.CENTER)

    fw, fh = (num / Pango.SCALE / 2 for num in layout.get_size())
    ctx.move_to(alloc.width / 2 - fw, alloc.height / 2 - fh)
    PangoCairo.show_layout(ctx, layout)
    ctx.stroke()


def _draw_segment(ctx, alloc, layer, max_layers, deg_a, deg_b, is_selected, bg_col):
    """Draw a radial segment on the context ctx with the following params:

    layer: The segment layer to draw (or "how far from the midpoint we are")
    max_layers: How many layerst there are at max.
    deg_a: Starting angle of the segment in degree.
    deg_b: Ending angle of the segment in degree.

    The dimensions of ctx are given by size in pixels.
    Assumption: alloc.width == alloc.height, so the area is rectangular.
    """
    # Convenience addition
    max_plus_one = max_layers + 1

    mid_x, mid_y = alloc.width / 2, alloc.height / 2
    mid = min(mid_x, mid_y)

    radius_a_px = (layer / max_plus_one) * mid
    radius_b_px = radius_a_px + (1 / max_plus_one) * mid

    # Draw the actual segment path
    ctx.arc(mid_x, mid_y, radius_a_px, deg_a, deg_b)
    ctx.arc_negative(mid_x, mid_y, radius_b_px, deg_b, deg_a)
    ctx.close_path()

    # Calculate the color as HSV
    h = deg_a / (math.pi * 2)
    s = 0.9 if is_selected else 0.66
    v = 1 - (layer / (max_plus_one + 1)) * (0.1 if is_selected else 0.6)

    # Fill it with the color
    ctx.set_source_rgb(*colorsys.hsv_to_rgb(h, s, v))
    ctx.fill_preserve()

    # Draw a white border around
    ctx.set_source_rgb(bg_col.red, bg_col.green, bg_col.blue)
    ctx.set_line_width(0.75)
    ctx.stroke()


def _draw_tooltip(ctx, alloc, x, y, layer, text):
    ctx.set_source_rgba(0, 0, 0, 0.9 - (layer / 4))
    ctx.arc(x, y, 5, 0, 2 * math.pi)
    ctx.fill()

    # TODO
    m = alloc.width / 2
    if x == m:
        return

    s = (y - m) / (x - m)
    o = y = s * x

    # ctx.move_to(x, y)
    ctx.line_to(x + 10, y + 10)
    ctx.stroke()


class ShredderChart(Gtk.DrawingArea):
    def __init__(self):
        Gtk.DrawingArea.__init__(self)
        self.connect('draw', self._on_draw)

        self.add_events(self.get_events() |
            Gdk.EventMask.POINTER_MOTION_MASK
        )

        self.connect('motion-notify-event', self._on_motion)

    def _on_draw(self, area, ctx):
        pass

    def _on_motion(self, area, event):
        pass

class Segment:
    def __init__(self, layer, degree, size):
        self.layer, self.degree, self.size = layer, degree, size
        self.is_selected = False

    def draw(self, ctx, alloc, max_layers, bg_col):
        _draw_segment(
            ctx, alloc,
            self.layer, max_layers,
            self.degree, self.degree + self.size,
            self.is_selected,
            bg_col
        )

    def hit(self, layer, deg):
        if self.layer != layer:
            self.is_selected = False
        else:
            self.is_selected = self.degree <= deg <= self.degree + self.size

        return self.is_selected

    def middle_point(self, alloc, max_layers):
        """Calculate the middle point of the segment.

        The middle point is here defined as the mid between
        start and end degree with the radius between both ends.
        This is used to determine the place where to stick the
        tooltip.
        """
        mid_x, mid_y = alloc.width / 2, alloc.height / 2
        r = ((self.layer + 0.5) / (max_layers + 1)) * min(mid_x, mid_y)
        d = self.degree + self.size / 2
        return mid_x + r * math.cos(d), mid_y + r * math.sin(d)


class ShredderRingChart(ShredderChart):
    def __init__(self):
        ShredderChart.__init__(self)

        self._timeout_id = None

        # TODO
        self.max_layers = 4
        self.segments = [
            Segment(1, 0, math.pi / 2),
            Segment(2, 0, math.pi / 4),
            Segment(3, 0, math.pi / 8),
            Segment(1, math.pi / 2, math.pi / 2),
            Segment(2, math.pi / 2, math.pi / 2 * 0.9),
            Segment(3, math.pi / 2, math.pi / 2 * 0.8),
            Segment(1, math.pi, math.pi),
            Segment(2, math.pi, math.pi * 0.9),
            Segment(3, math.pi, math.pi * 0.8)
        ]

    def _on_draw(self, area, ctx):
        # Figure out the background color of the drawing area
        bg_col = self.get_toplevel().get_style_context().get_background_color(0)
        alloc = area.get_allocation()

        # Draw the center text:
        _draw_center_text(
            ctx, alloc, '<span color="grey"><big>190 GB</big></span>'
        )

        print('---')
        for segment in self.segments:
            segment.draw(ctx, alloc, self.max_layers, bg_col)

            x, y = segment.middle_point(alloc, self.max_layers)
            _draw_tooltip(ctx, alloc, x, y, segment.layer, 'Bärbel')
            print(x, y)

    def _on_tooltip_timeout(self, segment):
        print(segment)
        self._timeout_id = None

    def _on_motion(self, area, event):
        alloc = area.get_allocation()
        mid_x, mid_y = alloc.width / 2, alloc.height / 2
        mid = min(mid_x, mid_y)

        # Calculate the degree between the vectors
        # a = (event.x + m, event.y + m) and (0, 1)
        x, y = event.x - mid_x, event.y - mid_y
        xy_abs = math.sqrt(x * x + y * y)
        cos = x / xy_abs

        if y < 0:
            # upper half
            selected_deg = (2 * math.pi) - math.acos(cos)
        else:
            # lower half
            selected_deg = math.acos(cos)

        # Check which layer we are operating on.
        selected_layer = math.floor(xy_abs * (self.max_layers + 1) / mid)

        hit_segment = None
        for segment in self.segments:
            if segment.hit(selected_layer, selected_deg):
                hit_segment = segment

        if self._timeout_id is not None:
            GLib.source_remove(self._timeout_id)
            self._timeout_id = None

        if hit_segment:
            self._timeout_id = GLib.timeout_add(
                250, self._on_tooltip_timeout, segment
            )

        self.queue_draw()


class ShredderChartStack(Gtk.Stack):
    LOADING = 'loading'
    DIRECTORY = 'directory'
    GROUP = 'group'

    def __init__(self):
        Gtk.Stack.__init__(self)

        self.spinner = Gtk.Spinner()
        self.spinner.start()
        self.add_named(self.spinner, 'loading')

        self.chart = ShredderRingChart()
        self.add_named(self.chart, 'directory')


if __name__ == '__main__':
    from gi.repository import GLib

    area = ShredderRingChart()

    win = Gtk.Window()
    win.set_size_request(500, 700)
    win.connect('destroy', Gtk.main_quit)
    win.add(area)
    win.show_all()

    Gtk.main()
