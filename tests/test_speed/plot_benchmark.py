#!/usr/bin/env python
# encoding: utf-8

import sys
import json
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
    labels = ['Run #' + str(i) for i in range(n_runs)]
    bar_chart.x_labels = labels + ['Average']

    for program in sorted(data['programs']):
        result = data['programs'][program]

        numbers = []
        for key in [str(run + 1) for run in range(n_runs)] + ['average']:
            value, cpu_usage = result['numbers'][key]

            numbers.append({
                'value': round(value, 3),
                'label': 'Memory: {mem} MB | CPU: {cpu}% | {ver}'.format(
                    ver=result['version'],
                    mem=round(result['memory'], 2),
                    cpu=cpu_usage
                ),
                'xlabel': result.get('website')
            })

        bar_chart.add(program, numbers)

    return bar_chart.render()


if __name__ == '__main__':
    with open(sys.argv[1], 'r') as handle:
        data = json.loads(handle.read())

    svg = plot(data)
    with open(sys.argv[2], 'wb') as handle:
        handle.write(svg)
