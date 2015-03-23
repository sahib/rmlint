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

from gi.repository import Pango
from gi.repository import PangoCairo


def _draw_center_text(ctx, x, y, text, font_size=10, do_draw=True):
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
    if do_draw:
        ctx.move_to(x - fw, y - fh)
        PangoCairo.show_layout(ctx, layout)
        ctx.stroke()

    return fw, fh


def _draw_rounded(ctx, area, radius):
    """draws rectangles with rounded (circular arc) corners"""
    a, b, c, d = area
    mpi2 = math.pi / 2
    ctx.arc(a + radius, c + radius, radius, 2 * mpi2, 3 * mpi2)
    ctx.arc(b - radius, c + radius, radius, 3 * mpi2, 4 * mpi2)
    ctx.arc(b - radius, d - radius, radius, 0 * mpi2, 1 * mpi2)
    ctx.arc(a + radius, d - radius, radius, 1 * mpi2, 2 * mpi2)
    ctx.close_path()
    ctx.fill()


def _draw_segment(
    ctx, alloc, layer, max_layers, deg_a, deg_b, is_selected, bg_col
):
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

    # Fill it with the color: Add a bit of highlight on start & end.
    pattern = cairo.RadialGradient(
        mid_x, mid_y, radius_a_px, mid_x, mid_y, radius_b_px
    )

    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    pattern.add_color_stop_rgb(0.0, r, g + 0.05, b + 0.05)
    pattern.add_color_stop_rgb(0.1, r, g, b)
    pattern.add_color_stop_rgb(0.9, r, g, b)
    pattern.add_color_stop_rgb(1.0, r, g + 0.05, b + 0.05)
    ctx.set_source(pattern)
    ctx.fill_preserve()

    # Draw a white border around
    r, g, b = colorsys.hsv_to_rgb(h, s, v - 0.5)
    ctx.set_source_rgba(r, g, b, 0.5)
    ctx.set_line_width(3)
    ctx.stroke_preserve()

    ctx.set_source_rgb(bg_col.red, bg_col.green, bg_col.blue)
    ctx.set_line_width(1.5)
    ctx.stroke()


def _draw_tooltip(ctx, alloc, x, y, dist, layer, angle, text):
    ctx.set_source_rgba(0, 0, 0, 1.0)
    ctx.arc(x, y, 5, 0, 2 * math.pi)
    ctx.fill()

    fw, fh = _draw_center_text(ctx, 0, 0, text, do_draw=False)

    w2, h2 = alloc.width / 2 - dist - fw, alloc.height / 2 - dist - fh
    angle_norm = math.fmod(math.fabs(angle), math.pi / 2)
    uneven = math.floor(angle / (math.pi / 2)) % 2 == 0

    # Do not ask. That was evolutionary.
    if not uneven:
        w2, h2 = h2, w2

    # Check where to paint the new point
    if angle_norm < math.atan2(h2, w2):
        new_x = w2
        new_y = math.tan(angle_norm) * new_x
    else:
        new_y = h2
        new_x = new_y / math.tan(angle_norm)

    # Flip the x/y into the right quadrant
    if angle < math.pi / 2:
        pass  # already in the right angle.
    elif angle < math.pi:
        new_x, new_y = -new_y, new_x
    elif angle < (math.pi + math.pi / 2):
        new_x, new_y = -new_x, -new_y
    elif angle < 2 * math.pi:
        new_x, new_y = new_y, -new_x

    # Finally, move from first quadrant to the whole.
    new_x = new_x + alloc.width / 2
    new_y = new_y + alloc.height / 2

    ctx.set_source_rgba(0, 0, 0, 0.3)
    ctx.move_to(x, y)
    ctx.line_to(new_x, new_y)
    ctx.stroke()

    ctx.set_source_rgba(0, 0, 0, 1.0)
    tip_w, tip_h = fw + 5, fh + 3
    _draw_rounded(
        ctx, (new_x - tip_w, new_x + tip_w, new_y - tip_h, new_y + tip_h), 3
    )
    ctx.fill()

    ctx.set_source_rgba(0.8, 0.8, 0.8, 1.0)
    _draw_center_text(ctx, new_x, new_y, text, do_draw=True)


class ShredderChart(Gtk.DrawingArea):
    def __init__(self):
        Gtk.DrawingArea.__init__(self)
        self.connect('draw', self._on_draw)

        self.add_events(self.get_events() | Gdk.EventMask.POINTER_MOTION_MASK)
        self.connect('motion-notify-event', self._on_motion)

    def _on_draw(self, area, ctx):
        pass

    def _on_motion(self, area, event):
        pass


class Segment:
    def __init__(self, layer, degree, size):
        self.layer, self.degree, self.size = layer, degree, size
        self.degree = math.fmod(self.degree, math.pi * 2)
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
        # Middle point of the whole diagram
        mid_x, mid_y = alloc.width / 2, alloc.height / 2

        # Distance from (mid_x, mid_y) to middle point
        rad = ((self.layer + 0.5) / (max_layers + 1)) * min(mid_x, mid_y)

        # Half of the degree range + start
        deg = self.degree + self.size / 2
        return mid_x + rad * math.cos(deg), mid_y + rad * math.sin(deg)

    def middle_angle(self):
        return self.degree + self.size / 2


class ShredderRingChart(ShredderChart):
    def __init__(self):
        ShredderChart.__init__(self)

        # Id of the
        self._timeout_id = None

        self.add = 0
        self._set_segments()
        GLib.timeout_add(50, self._set_segments)

    def _set_segments(self):
        # TODO: remove test.
        self._segment_list = []
        self.add += math.pi / 512
        self.max_layers = 4
        s = [
            Segment(1, self.add + 0, math.pi / 2),
            Segment(2, self.add + 0, math.pi / 4),
            Segment(3, self.add + 0, math.pi / 8),
            Segment(1, self.add + math.pi / 2, math.pi / 2),
            Segment(2, self.add + math.pi / 2, math.pi / 2 * 0.9),
            Segment(3, self.add + math.pi / 2, math.pi / 2 * 0.8),
            Segment(1, self.add + math.pi, math.pi * 0.9),
            Segment(2, self.add + math.pi, math.pi * 0.8),
            Segment(3, self.add + math.pi, math.pi * 0.7),
            Segment(1, self.add + math.pi * 0.9 + math.pi, math.pi * 0.1),
            Segment(2, self.add + math.pi * 0.9 + math.pi, math.pi * 0.1),
            Segment(3, self.add + math.pi * 0.9 + math.pi, math.pi * 0.1)
        ]

        for seg in s:
            self.add_segment(seg)

        self.queue_draw()
        return True

    def add_segment(self, segment):
        self._segment_list.append(segment)

    def _on_draw(self, area, ctx):
        # Figure out the background color of the drawing area
        bg_col = self.get_toplevel().get_style_context().get_background_color(0)
        alloc = area.get_allocation()

        # Draw the center text:
        _draw_center_text(
            ctx, alloc.width / 2, alloc.height / 2,
            '<span color="grey"><small>190 GB</small></span>'
        )

        # Extract the segments sorted by layer
        segs = sorted(self._segment_list, key=lambda e: e.layer, reverse=True)

        for segment in segs:
            segment.draw(ctx, alloc, self.max_layers, bg_col)

        for segment in segs:
            if segment.layer > 1:
                continue

            x, y = segment.middle_point(alloc, self.max_layers)
            _draw_tooltip(
                ctx, alloc, x, y, 10, segment.layer,
                segment.middle_angle(), 'Bärbel'
            )

    def _on_tooltip_timeout(self, segment):
        """Called once the mouse stayed over a segment for a longer time.
        """
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
        for segment in self._segment_list:
            if segment.hit(selected_layer, selected_deg):
                hit_segment = segment

        if self._timeout_id is not None:
            id_, old_segment = self._timeout_id
            if segment is old_segment:
                # There's already a timer running.
                hit_segment = None
            else:
                GLib.source_remove(id_)
                self._timeout_id = None

        if hit_segment:
            id_ = GLib.timeout_add(
                250, self._on_tooltip_timeout, segment
            )
            self._timeout_id = (id_, segment)

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
    area = ShredderRingChart()

    win = Gtk.Window()
    win.set_size_request(300, 500)
    win.connect('destroy', Gtk.main_quit)
    win.add(area)
    win.show_all()

    Gtk.main()
