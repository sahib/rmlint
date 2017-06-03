#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import os
import sys
import json
import glob
import argparse
import statistics

# External:
import pygal


# from pygal.style import LightSolarizedStyle as Style
from pygal.style import DefaultStyle as Style


VALID_ATTRS = {
    'timing': 0,
    'cpu_usage': 1,
    'peakmem': 2,
    'dupes': 3,
    'sets': 4
}

Style.background = '#FFFFFF'

CONFIG = pygal.Config()
CONFIG.human_readable = True
CONFIG.tooltip_fancy_mode = True
CONFIG.style = Style


def unpack(chart, data, bench_name, add_x_labels=True):
    chart.title = '{name} comparison on {path}'.format(
        name=bench_name,
        path=data['metadata']['path']
    )

    if bench_name:
        chart.y_title = 'Averaged {name} over {rn} runs'.format(
            rn=data['metadata']['n_sub_runs'],
            name=bench_name
        )

    n_runs = data['metadata']['n_runs']
    if add_x_labels:
        labels = ['Run #' + str(i + 1) for i in range(n_runs)]
        chart.x_labels = labels + ['Average']

    for program in sorted(data['programs']):
        result = data['programs'][program]

        if len(result["numbers"]) < n_runs:
            continue

        points = []
        for key in [str(run + 1) for run in range(n_runs)] + ['average']:
            points.append(result['numbers'][key])

        yield program, result, points


def format_tooltip(program, version):
    return '{name} ({ver})'.format(
        name=program,
        ver=version
    )


def _plot_generic(data, chart, name, key):
    for program, metadata, points in unpack(chart, data, name):
        numbers = []

        for point in points:
            numbers.append({
                'value': round(key(point), 3),
                'label': format_tooltip(
                    program,
                    metadata['version']
                ),
                'xlink': metadata.get('website')
            })

        chart.add(program, numbers)


def plot_memory(data):
    chart = pygal.StackedBar(CONFIG)
    _plot_generic(
        data, chart, 'Peakmem', lambda p: p[VALID_ATTRS['peakmem']]
    )

    return chart.render(is_unicode=True)


def plot_timing(data):
    chart = pygal.Bar(CONFIG, logarithmic=True)
    _plot_generic(
        data, chart, 'Timing', lambda p: p[VALID_ATTRS['timing']]
    )

    return chart.render(is_unicode=True)


def plot_cpu_usage(data):
    chart = pygal.Bar(CONFIG)
    _plot_generic(
        data, chart, 'CPU usage', lambda p: p[VALID_ATTRS['cpu_usage']]
    )

    return chart.render(is_unicode=True)


def plot_found_results(data):
    chart = pygal.Bar(CONFIG)

    results = []
    unpacked = unpack(chart, data, 'Found results', add_x_labels=True)
    for program, metadata, points in unpacked:
        average = points[-1]

        results.append(([
            average[VALID_ATTRS['dupes']],
            average[VALID_ATTRS['sets']],
            #statistics.variance([p[2] for p in points[:-1]]),
            #statistics.variance([p[3] for p in points[:-1]])
        ], program))

    labels = [
        'Dupes found',
        'Set of dupes',
        'Dupevariance between runs',
        'Setvariance between runs'
    ]

    results.sort()

    for point, program in results:
        chart.add(
            program, [{
                'value': value,
                'label': '{v} {l}'.format(
                    v=value, l=labels[idx]
                )
            } for idx, value in enumerate(point)]
        )

    chart.x_labels = ['Duplicates', 'Originals']
    return chart.render_table(style=True, total=False)


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        'input_dir', help='Directory with bench files'
    )
    parser.add_argument(
        'output_dir', help='Where to store the plots', nargs='?'
    )

    return parser.parse_args()


def guess_output_dir(input_dir):
    if input_dir.startswith('bench-'):
        _, stamp = input_dir.split('-', 1)
        return 'plot-' + stamp

    return 'plot-output'


def main():
    options = parse_arguments()

    if not options.output_dir:
        options.output_dir = guess_output_dir(options.input_dir)

    os.makedirs(options.output_dir, exist_ok=True)

    PLOT_FUNCS = {
        'timing': (plot_timing, False),
        'memory': (plot_memory, False),
        'cpu_usage': (plot_cpu_usage, False),
        'found_items': (plot_found_results, True)
    }

    for path in glob.glob(os.path.join(options.input_dir, '*.json')):
        with open(path, 'r') as handle:
            data = json.loads(handle.read())
            for attr, (plot_func, is_table) in PLOT_FUNCS.items():
                svg_or_table = plot_func(data)
                suffix = '.html' if is_table else '.svg'

                output_path = os.path.join(
                    options.output_dir,
                    attr + '-' + os.path.basename(path) + suffix
                )

                print('Writing:', output_path)
                with open(output_path, 'w') as handle:
                    handle.write(svg_or_table)


if __name__ == '__main__':
    main()
