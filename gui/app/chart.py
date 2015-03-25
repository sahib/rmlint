#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import math
import colorsys

from operator import itemgetter
from itertools import groupby

# Internal:
from app.util import size_to_human_readable

# External:
import cairo

from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import GLib

from gi.repository import Pango
from gi.repository import PangoCairo

###########################################################
# NOTE: This code is inspired by the baobab code,         #
#       since it's very clean and a good read.            #
#       However this code is not ported, but rewritten.   #
###########################################################

# TODO
class Column:
    """Column Enumeration to avoid using direct incides.
    """
    SELECTED, PATH, SIZE, COUNT, MTIME, TAG, TOOLTIP = range(7)

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


TANGO_TABLE = [
    (0.000, 0.829, 0.94),
    (0.096, 0.747, 0.99),
    (0.104, 0.527, 0.91),
    (0.251, 0.775, 0.89),
    (0.590, 0.451, 0.82),
    (0.850, 0.279, 0.68)
]


def _hsv_by_degree(degree):
    percent = degree / (2 * math.pi)
    idx = percent * len(TANGO_TABLE)
    h, s, v = TANGO_TABLE[int(idx) - 1]
    return h, s, v


def _draw_segment(
    ctx, alloc, layer, max_layers, deg_a, deg_b, is_selected, bg_col
):
    """Draw a radial segment on the context ctx with the following params:

    layer: The segment layer to draw (or "how far from the midpoint we are")
    max_layers: How many layerst there are at max.
    deg_a: Starting angle of the segment in degree.
    deg_b: Ending angle of the segment in degree.

    The dimensions of ctx are given by size in pixels.
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
    h, s, v = _hsv_by_degree(deg_a / 2 + deg_b / 2)

    # "Fix" the color for some special cases
    s += 0.2 if is_selected else -0.05
    v -= (layer / (max_plus_one + 1)) / 4

    if is_selected:
        v += 0.2

    # Fill it with the color: Add a bit of highlight on start & end
    # to round it up. This should stay rather subtle of course.
    pattern = cairo.RadialGradient(
        mid_x, mid_y, radius_a_px, mid_x, mid_y, radius_b_px
    )

    rn, gn, bn = colorsys.hsv_to_rgb(h, s, v)
    rh, gh, bh = colorsys.hsv_to_rgb(h, s + 0.05, v + 0.15)
    rl, gl, bl = colorsys.hsv_to_rgb(h, s - 0.05, v - 0.25)
    off = 42 / min(alloc.width, alloc.height)

    pattern.add_color_stop_rgb(0.0, rl, gl, bl)
    pattern.add_color_stop_rgb(off * 2, rn, gn, bn)
    pattern.add_color_stop_rgb(1 - off, rn, gn, bn)
    pattern.add_color_stop_rgb(1.0, rh, gh, bh)
    ctx.set_source(pattern)
    ctx.fill_preserve()

    # Draw a little same colored border around.
    thickness = min(alloc.width, alloc.height) / 160

    r, g, b = colorsys.hsv_to_rgb(h, s, v - 0.5)
    ctx.set_source_rgba(r, g, b, 0.5)
    ctx.set_line_width(thickness * 1.5)
    ctx.stroke_preserve()

    # Draw a (probably) white border around
    ctx.set_source_rgb(bg_col.red, bg_col.green, bg_col.blue)
    ctx.set_line_width(thickness)
    ctx.stroke()

    ctx.set_line_width(1)


def _draw_tooltip(ctx, alloc, x, y, dist, layer, angle, text):
    # Draw the anchor circle on the segment
    ctx.set_source_rgba(0, 0, 0, 1.0)
    ctx.arc(x, y, 3, 0, 2 * math.pi)
    ctx.fill()

    # Guess the font width used for the tooltip text.
    fw, fh = _draw_center_text(ctx, 0, 0, text, do_draw=False)

    # Bounding box onto which the tooltips will be projected
    w2, h2 = alloc.width / 2 - dist - fw, alloc.height / 2 - dist - fh
    angle_norm = math.fmod(math.fabs(angle), math.pi / 2)

    # Check if we are in the second or last quadrant.
    # In that case we flip the dimensions in order
    # to make the flipping logic below work.
    if math.floor(angle / (math.pi / 2)) % 2:
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
    new_x += alloc.width / 2
    new_y += alloc.height / 2

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

# TODO: Move to (and create) model.py
def _dfs(model, iter_, layer=1):
    """Generator for a depth first traversal in a TreeModel.
    Yields a GtkTreeIter for all iters below and after iter_.
    """
    while iter_ is not None:
        yield iter_, layer
        child = model.iter_children(iter_)
        if child is not None:
            yield from _dfs(model, child, layer + 1)

        iter_ = model.iter_next(iter_)


class ShredderChart(Gtk.DrawingArea):
    def __init__(self, model):
        Gtk.DrawingArea.__init__(self)
        self.connect('draw', self._on_draw)

        self._model = model

        self.add_events(self.get_events() | Gdk.EventMask.POINTER_MOTION_MASK)
        self.connect('motion-notify-event', self._on_motion)

    def _on_draw(self, area, ctx):
        pass

    def _on_motion(self, area, event):
        pass


class Segment:
    def __init__(self, layer, degree, size, tooltip=None):
        self.children = []
        self.layer, self.degree, self.size = layer, degree, size
        self.degree = math.fmod(self.degree, math.pi * 2)
        self.tooltip = tooltip
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
    def __init__(self, model):
        ShredderChart.__init__(self, model)

        # Id of the tooltip timeout
        self._timeout_id = None
        self._segment_list = []
        self.max_layers = 0
        self.total_size = 1

    def add_segment(self, segment):
        self._segment_list.append(segment)

    def _group_nodes(self, model, root):
        nodes = sorted(_dfs(model, root), key=itemgetter(1))
        grouped, layer_off = {}, 0

        for layer, group in groupby(nodes, key=itemgetter(1)):
            grouped[layer] = list(group)

        return grouped

    def _calculate_angles(self, nodes, model, root, pseudo_segment):
        angles = {}
        total_size = model[root][Column.SIZE]

        for layer, group in nodes.items():
            for iter_, _ in group:
                up_path = model.get_path(iter_)
                up_path.up()

                parent = angles.get(str(up_path)) if up_path else None
                if parent is None:
                    # Assume an invisible segment on layer 0
                    parent, prev_size = pseudo_segment, total_size
                else:
                    prev_size = model[model.get_iter(str(up_path))][Column.SIZE]

                if prev_size:
                    angle_len = parent.size * model[iter_][Column.SIZE] / prev_size
                else:
                    angle_len = 0

                angle = parent.degree + sum(child.size for child in parent.children)
                segment = Segment(layer, angle, angle_len, model[iter_][Column.PATH])

                parent.children.append(segment)
                self.add_segment(segment)
                angles[str(model.get_path(iter_))] = segment

    def render(self, model, root):
        nodes = self._group_nodes(model, root)
        self.max_layers = max(nodes.keys()) + 1

        pseudo_segment = Segment(0, 0, 2 * math.pi)
        self._calculate_angles(nodes, model, root, pseudo_segment)
        self.total_size = model[model.get_iter_from_string('0')][Column.SIZE]

    def _on_draw(self, area, ctx):
        # Figure out the background color of the drawing area
        bg_col = self.get_toplevel().get_style_context().get_background_color(0)
        alloc = area.get_allocation()

        # Draw the center text:
        _draw_center_text(
            ctx, alloc.width / 2, alloc.height / 2,
            '<span color="#333"><small>{size}</small></span>'.format(
                size=size_to_human_readable(self.total_size)
            ),
            font_size=min(alloc.width, alloc.height) / 42
        )

        # Extract the segments sorted by layer
        # Also filter very small segments for performance reasons
        segs = filter(lambda seg: seg.size > math.pi / 256, self._segment_list)
        segs = sorted(segs, key=lambda e: e.layer, reverse=True)

        for segment in segs:
            segment.draw(ctx, alloc, self.max_layers, bg_col)

        for segment in segs:
            # if segment.layer > 1:
            #     continue
            if segment.size < math.pi / 16:
                continue

            x, y = segment.middle_point(alloc, self.max_layers)
            _draw_tooltip(
                ctx, alloc, x, y, 10, segment.layer,
                segment.middle_angle(), segment.tooltip
            )

    def _on_tooltip_timeout(self, segment):
        """Called once the mouse stayed over a segment for a longer time.
        """
        if self._timeout_id:
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
            GLib.source_remove(self._timeout_id)
            self._timeout_id = None

        if hit_segment:
            id_ = GLib.timeout_add(
                250, self._on_tooltip_timeout, segment
            )
            self._timeout_id = id_

        self.queue_draw()


class ShredderChartStack(Gtk.Stack):
    LOADING = 'loading'
    DIRECTORY = 'directory'
    GROUP = 'group'

    def __init__(self, model):
        Gtk.Stack.__init__(self)

        self.spinner = Gtk.Spinner()
        self.spinner.start()
        self.add_named(self.spinner, 'loading')

        self.chart = ShredderRingChart(model)
        self.add_named(self.chart, 'directory')

    def render(self, model, root):
        self.chart.render(model, root)


if __name__ == '__main__':
    model = Gtk.TreeStore(str, int)

    # (!) HACK FOR MAIN (!)
    Column.PATH = 0
    Column.SIZE = 1

    # root = model.append(None, ('', 6000))
    # home = model.append(root, ('home', 6000))
    sahib = model.append(None, ('sahib', 6000))

    docs = model.append(sahib, ('docs', 2000))
    model.append(docs, ('stuff.pdf', 500))

    more = model.append(docs, ('more', 1500))
    model.append(more, ('stuff.pdf.1', 700))
    model.append(more, ('stuff.pdf.2', 600))
    model.append(more, ('stuff.pdf.3', 200))

    music = model.append(sahib, ('music', 4000))
    model.append(music, ('1.mp3', 1000))

    sub = model.append(music, ('sub', 3000))
    model.append(sub, ('2.mp3', 1200))
    model.append(sub, ('3.mp3', 1200))
    model.append(sub, ('4.mp3', 600))

    area = ShredderRingChart(model)
    area.render(model, model.get_iter_from_string('0'))

    win = Gtk.Window()
    win.set_size_request(300, 500)
    win.connect('destroy', Gtk.main_quit)
    win.add(area)
    win.show_all()

    Gtk.main()
