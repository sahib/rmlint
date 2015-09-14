#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import os
import sys
import json
import glob
import argparse

# External:
import pygal

from pygal.style import LightSolarizedStyle


VALID_ARGS = {
    'timing': 0,
    'cpu_usage': 1,
    'dupes': 2,
    'sets': 3
}


def plot(data, attr_key=VALID_ARGS['timing']):
    bar_chart = pygal.Bar(style=LightSolarizedStyle)
    bar_chart.title = 'Performance comparasion on {path}'.format(
        path=data['metadata']['path']
    )
    bar_chart.y_title = 'Averaged seconds over {rn} runs'.format(
        rn=data['metadata']['n_sub_runs']
    )

    n_runs = data['metadata']['n_runs']
    labels = ['Run #' + str(i + 1) for i in range(n_runs)]
    bar_chart.x_labels = labels + ['Average']

    for program in sorted(data['programs']):
        result = data['programs'][program]

        numbers = []
        for key in [str(run + 1) for run in range(n_runs)] + ['average']:
            timing, cpu_usage, dupes, sets = result['numbers'][key]
            value = result['numbers'][key][attr_key]

            numbers.append({
                'value': round(value, 3),
                'label': '{name} ({ver}): Peak: {mem}M CPU: {cpu}%'.format(
                    name=program,
                    ver=result['version'],
                    mem=round(result['memory'], 2),
                    cpu=cpu_usage
                ),
                'xlabel': result.get('website')
            })

        bar_chart.add(program, numbers)

    return bar_chart.render()


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "input_dir", help="Directory with bench files"
    )
    parser.add_argument(
        "output_dir", help="Where to store the plots", nargs='?'
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

    for path in glob.glob(os.path.join(options.input_dir, '*.json')):
        with open(path, 'r') as handle:
            data = json.loads(handle.read())
            for attr, attr_id in VALID_ARGS.items():
                svg = plot(data, attr_id)

                output_path = os.path.join(
                    options.output_dir,
                    attr + '-' + os.path.basename(path) + '.svg'
                )

                with open(output_path, 'wb') as handle:
                    handle.write(svg)


if __name__ == '__main__':
    main()
