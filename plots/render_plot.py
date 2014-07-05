import sys
from collections import OrderedDict

import numpy as np
import matplotlib
#matplotlib.use('Agg')

import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator


DATA = OrderedDict()
COLORS = {
    'sha512': '#ff0000',
    'sha256': '#ff7f00',
    'sha1': '#ffff00',
    'md5': '#00ff00',
    'murmur': '#0000ff',
    'spooky': '#4b0082',
    'city': '#8f00ff'
}

if __name__ == '__main__':
    X = set()

    for path in sorted(sys.argv[1:]):
        print(path)
        name = None
        with open(path, 'r') as f:
            row = OrderedDict()
            for line in f:
                if line.startswith('#'):
                    X.add(int(line[1:-3].strip()))
                else:
                    checksum, time, method = filter(None, line.split(' '))
                    DATA.setdefault(path, OrderedDict()).setdefault(method.strip(), []).append(float(time[:-1]))

    font = {'fontname': 'Humor Sans'}

    matplotlib.rc('xtick', labelsize=10)
    matplotlib.rc('ytick', labelsize=10)
    f, axes = plt.subplots(len(DATA) // 4, 4, sharex=True, sharey=False, figsize=(30, 15))
    axes = list(axes[0]) + list(axes[1])
    X = sorted(X)
    plt.xlim(1, 1024)

    plots = []
    plt.xkcd()
    for idx, path in enumerate(DATA):
        data = DATA[path]
        axes[idx].set_title(path[5:-4] + ' Megabyte Buffer', size=12, **font)

        y_ticks = []
        for key in data:
            Y = data[key]
            print(X, Y)
            Y = [v + 0.0001 for v in Y]

            if idx >= 4:
                axes[idx].set_xlabel('File size in Megabyte', size=11, **font)

            if idx is 0 or idx is 4:
                axes[idx].set_ylabel('Time in seconds', size=11, **font)
            plots.append(axes[idx].plot(X, Y, '-o', color=COLORS[key], ms=5, lw=2, alpha=0.7, mfc='red', label=key)[0])
            axes[idx].grid()
            y_ticks.append(max(Y))

        y_ticks = sorted(y_ticks)
        y_ticks = [y_ticks[0]] + y_ticks[3:]

        # axes[idx].set_xscale('log', basex=2)
        # axes[idx].set_yscale('log', basex=2)

        axes[idx].set_xticks(X[6:])
        axes[idx].set_yticks(y_ticks)
        axes[idx].set_xticklabels(axes[idx].get_xticks(), size=9, **font)
        axes[idx].set_yticklabels(axes[idx].get_yticks(), size=9, **font)
        axes[idx].legend(plots, data.keys(), loc=2, prop={'size': 10})

    # plt.show()
    plt.savefig('/tmp/test.pdf', format='pdf', dpi=120)
