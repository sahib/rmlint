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

from pygal.style import LightSolarizedStyle


VALID_ATTRS = {
    'timing': 0,
    'cpu_usage': 1,
    'dupes': 2,
    'sets': 3
}

LightSolarizedStyle.background = '#FFFFFF'

CONFIG = pygal.Config()
CONFIG.human_readable = True
CONFIG.tooltip_fancy_mode = True
CONFIG.style = LightSolarizedStyle


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

        points = []
        for key in [str(run + 1) for run in range(n_runs)] + ['average']:
            points.append(result['numbers'][key])

        yield program, result, points


def format_tooltip(program, memory, version):
    return '{name} ({ver}): Peakmem: {mem}M'.format(
        name=program,
        ver=version,
        mem=round(memory, 2)
    )


def _plot_generic(data, chart, name, key):
    for program, metadata, points in unpack(chart, data, name):
        numbers = []

        for point in points:
            numbers.append({
                'value': round(key(point), 3),
                'label': format_tooltip(
                    program,
                    metadata['memory'],
                    metadata['version']
                ),
                'xlink': metadata.get('website')
            })

        chart.add(program, numbers)


def plot_memory(data):
    chart = pygal.Pie(CONFIG, inner_radius=.4)

    points = []
    unpacked = unpack(chart, data, 'memory', add_x_labels=False)
    for program, metadata, _ in unpacked:
        points.append((metadata['memory'], program))

    points.sort()
    for memory, program in points:
        chart.add(program, memory)

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
    chart = pygal.Pie(CONFIG, inner_radius=.4)

    results = []
    unpacked = unpack(chart, data, 'Found results', add_x_labels=False)
    for program, metadata, points in unpacked:
        average = points[-1]

        results.append(([
            average[VALID_ATTRS['dupes']],
            average[VALID_ATTRS['sets']],
            statistics.variance([p[2] for p in points[:-1]]),
            statistics.variance([p[3] for p in points[:-1]])
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

    return chart.render(is_unicode=True)


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
        'timing': plot_timing,
        'memory': plot_memory,
        'cpu_usage': plot_cpu_usage,
        'found_items': plot_found_results
    }

    for path in glob.glob(os.path.join(options.input_dir, '*.json')):
        with open(path, 'r') as handle:
            data = json.loads(handle.read())
            for attr, plot_func in PLOT_FUNCS.items():
                svg = plot_func(data)

                output_path = os.path.join(
                    options.output_dir,
                    attr + '-' + os.path.basename(path) + '.svg'
                )

                print('Writing:', output_path)
                with open(output_path, 'w') as handle:
                    handle.write(svg)


if __name__ == '__main__':
    main()
