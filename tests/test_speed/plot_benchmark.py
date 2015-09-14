#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import sys
import json
import argparse

# External:
import pygal

from pygal.style import LightSolarizedStyle


def plot(data):
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
            value, cpu_usage = result['numbers'][key]

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
        "input-dir", help="Directory with bench files"
    )
    parser.add_argument(
        "output-dir", help="Where to store the plots"
    )

    return parser.parse_args()


def guess_output_dir(input_dir):
    if input_dir.startswith('bench-'):
        _, stamp = input_dir.split('-', 1)
        return 'plot-' + stamp

    return 'plot-output'


def main():
    options = parse_arguments()
    print(options)

    if not options.output_dir:
        options.output_dir = guess_output_dir(options.input_dir)

    os.makedirs(options.output_dir, exist_ok=True)

    for path in glob.glob(os.path.join(options.input_dir, '*.json')):
        print(path)
        with open(path, 'r') as handle:
            data = json.loads(handle.read())
            svg = plot(data)

        output_path = os.path.join(
            options.output_dir,
            os.path.basename(path)
        )

        with open(output_path, 'wb') as handle:
            handle.write(svg)


if __name__ == '__main__':
    main()
