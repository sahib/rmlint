#!/usr/bin/env python
# encoding: utf-8

# Internal:
from app.util import get_theme_color

# External:
from gi.repository import Gtk


CHART_OPTIONS = {
    'axis': {
        'x': {
            'ticks': [],
            'rotate': 90,
        },
        'y': {
            'tickCount': 4,
        }
    },
    'background': {
        'color': '#eeeeff',
        'lineColor': '#444444'
    },
    'colorScheme': {
        'args': {
            'initialColor': 'blue',
        },
    },
    'legend': {
        'hide': True,
    },
    'padding': {
        'left': 30,
        'right': 30,
        'top': 30,
        'bottom': 30
    },
    'stroke': {
        'width': 3,
        'shadow': True
    },
    'title': 'Stuff.',
    'titleFontSize': 10
}


class ChartView(Gtk.DrawingArea):
    def __init__(self):
        Gtk.DrawingArea.__init__(self)
        self.connect('draw', self._on_draw)

        CHART_OPTIONS['colorScheme']['args']['initialColor'] = get_theme_color(
            self, True, Gtk.StateFlags.SELECTED
        )

    def _on_draw(self, _, ctx):
        options = {
            'legend': {'hide': True},
            'background': {'color': '#f0f0f0'},
        }
        chart = LineChart(ctx.get_target(), options)
        data = (
            ('dataSet 1', ((0, 1), (1, 3), (2, 2.5))),
            ('dataSet 2', ((0, 2), (1, 4), (2, 3))),
            ('dataSet 3', ((0, 5), (1, 1), (2, 0.5))),
        )

        chart.addDataset(data)
        chart.render()
