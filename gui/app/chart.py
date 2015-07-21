#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import math
import colorsys

# Internal:
from app.util import size_to_human_readable
from app.tree import Column

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
    mid_x, mid_y = alloc.width / 2, alloc.height / 2
    mid = min(mid_x, mid_y)

    radius_a_px = (layer / (max_layers + 1)) * mid
    radius_b_px = radius_a_px + (1 / (max_layers + 1)) * mid

    # Draw the actual segment path
    ctx.arc(mid_x, mid_y, radius_a_px, deg_a, deg_b)
    ctx.arc_negative(mid_x, mid_y, radius_b_px, deg_b, deg_a)
    ctx.close_path()

    # Calculate the color as HSV
    h, s, v = _hsv_by_degree(deg_a / 2 + deg_b / 2)

    # "Fix" the color for some special cases
    s += 0.2 if is_selected else -0.07

    # Make more distant segments darker
    v *= 1.25 - (layer / max_layers / 1.5)

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


class ShredderChart(Gtk.DrawingArea):
    def __init__(self):
        Gtk.DrawingArea.__init__(self)
        self.connect('draw', self._on_draw)

        self.add_events(
            self.get_events() |
            Gdk.EventMask.POINTER_MOTION_MASK |
            Gdk.EventMask.BUTTON_PRESS_MASK
        )
        self.connect('motion-notify-event', self._on_motion)
        self.connect('button-press-event', self._on_button_press_event)

    ##############################
    # TO BE OVERWRITTEN BY CHILD #
    ##############################

    def _on_draw(self, area, ctx):
        pass

    def _on_motion(self, area, event):
        pass

    def _on_button_press_event(self, area, event):
        pass


class Segment:
    def __init__(self, node, layer, degree, size, tooltip=None):
        self.node = node
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
    def __init__(self):
        ShredderChart.__init__(self)

        # Id of the tooltip timeout
        self._timeout_id = None
        self._segment_list = []
        self.max_layers = 0
        self.total_size = 1
        self._selected_segment = None

    def recursive_angle(self, node, angle, offset, layer_offset=0):
        """Calculates the angles of the segments and stores them in a
        list that is ordered by Z-Depth, so the plot appears to be layered
        with the root circle on top. This resembles a depth first traversal.
        """
        self._segment_list.append(Segment(
            node, node.depth - layer_offset,
            offset, angle, node[Column.PATH]
        ))

        child_offset = offset
        for child in node.children.values():
            child_angle = (child[Column.SIZE] / node[Column.SIZE]) * angle

            # Do not investigate smaller nodes:
            if child_angle > math.pi / 64:
                self.recursive_angle(child, child_angle, child_offset, layer_offset)
                # Remember deepest layer:
                self.max_layers = max(child.depth, self.max_layers)

            child_offset += child_angle


    def find_root(self, node):
        """Iterate to the first child that has more than one children"""
        if len(node.children) > 1:
            return node

        for child in node.children.values():
            return self.find_root(child)

        # Default to the actual root:
        return node

    def render(self, root):
        """Render `root` and all children of it as chart."""
        # Skip over duplicate full circles:
        virt_root = self.find_root(root)

        # Reset the segment list, fill it again by dfs and sort it.
        self._segment_list = []
        self.recursive_angle(virt_root, 2 * math.pi, 0, virt_root.depth - 1)
        self._segment_list.sort(key=lambda node: node.layer)

        # Make sure we show the right total size
        self.total_size = virt_root[Column.SIZE]

        # Make sure it gets rendered soon:
        self.queue_draw()

    def _on_draw(self, area, ctx):
        """Actual signal callback that triggers all the drawing."""

        # May happen on empty charts:
        if self.max_layers is 0:
            return False

        # Figure out the background color of the drawing area
        alloc = area.get_allocation()

        # Caluclate the font size of the inner label.
        # Make it smaller if not enough place but cut off at a size of 12
        inner_circle = (1 / self.max_layers)
        inner_circle *= min(alloc.width, alloc.height) / 2
        font_size = min(12, inner_circle / 3)

        # Draw the center text:
        _draw_center_text(
            ctx, alloc.width / 2, alloc.height / 2,
            '<span color="#333"><small>{size}</small></span>'.format(
                size=size_to_human_readable(self.total_size)
            ),
            font_size=font_size
        )

        bg = self.get_toplevel().get_style_context().get_background_color(0)
        for segment in reversed(self._segment_list):
            segment.draw(ctx, alloc, self.max_layers, bg)

        if self._selected_segment is None:
            return

        for segment in self._segment_list:
            if segment.layer != self._selected_segment.layer:
                continue

            if segment.size < math.pi / 32:
                continue

            x, y = segment.middle_point(alloc, self.max_layers)
            _draw_tooltip(
                ctx, alloc, x, y, 8, segment.layer,
                segment.middle_angle(), segment.tooltip
            )

    def _on_tooltip_timeout(self, segment):
        """Called once the mouse stayed over a segment for a longer time.
        """
        if self._timeout_id:
            self._selected_segment = segment
        else:
            self._selected_segment = None

        self.queue_draw()
        self._timeout_id = None

    def _hit(self, area, event, click_only=False):
        """Check what segments were hitten by a GdkEvent"""
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
                if click_only:
                    break

        return hit_segment

    def _on_motion(self, area, event):
        hit_segment = self._hit(area, event)

        if self._timeout_id is not None:
            GLib.source_remove(self._timeout_id)
            self._timeout_id = None
            self._selected_segment = None

        if hit_segment:
            id_ = GLib.timeout_add(
                250, self._on_tooltip_timeout, hit_segment
            )
            self._timeout_id = id_

        self.queue_draw()

    def _on_button_press_event(self, area, event):
        hit_segment = self._hit(area, event, click_only=True)
        if hit_segment is not None:
            self.render(hit_segment.node)


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

    def render(self, root):
        self.chart.render(root)


if __name__ == '__main__':
    from app.tree import PathTreeModel
    model = PathTreeModel(['/home/sahib'])

    def push(size, path):
        model.add_path(path, Column.make_row({'size': size}), True)

    push(500,  '/home/sahib/docs/stuff.pdf')

    for idx, size in enumerate((700, 600, 200)):
        push(size,  '/home/sahib/docs/more/' + 'stuff.pdf-' + str(idx))

    for idx in range(50):
        push(10,  '/home/sahib/docs/more/' + 'small.pdf-' + str(idx))

    for idx in range(10):
        push(100,  '/home/sahib/' + 'dummy-' + str(idx))

    push(1000, '/home/sahib/music/1.mp3')
    push(1200, '/home/sahib/music/sub/2.mp3')
    push(1200, '/home/sahib/music/sub/3.mp3')
    push(600,  '/home/sahib/music/sub/4.mp3')
    print(model.trie)

    area = ShredderRingChart()
    area.render(model.trie.root)

    win = Gtk.Window()
    win.set_size_request(300, 500)
    win.connect('destroy', Gtk.main_quit)
    win.add(area)
    win.show_all()

    Gtk.main()
