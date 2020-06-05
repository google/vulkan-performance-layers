#!/usr/bin/env python3

"""
A script for analyzing and comparing frame time layer logs.

Sample use:
    analyze_frametimes.py \
        --dataset baseline /my/path/baseline/**/frame_times.log \
        --dataset test /my/path/test/**/frame_times.log

In a addition to stats printed to stdout, the above command produces 3 plots:
1.  baseline_frametimes.png -- all baseline runs on a single plot.
2.  test_frametimes.png -- all test runs on a single plot.
3.  combined.png -- a plot with the best and median result from each dataset.
"""

import argparse
import csv
import subprocess
import sys
import os.path as path
import numpy as np

script_dir = path.dirname(path.realpath(__file__))
plot_script_path = path.join(script_dir, 'plot_frame_times.sh')

class FrameTimeResult(object):
    '''
    Represents frametimes from a single frame time layer log.
    '''
    TargetFPS = 60
    TargetFrameTime = 10 ** 9 / TargetFPS

    def __init__(self):
        self.path = ""
        self.run_name = ""
        self.raw_frametimes = []
        self.total_duration_ms = -1
        self.average_frametime_ms = -1
        self.percentile_frametime_ms = []

    @staticmethod
    def from_file(log_path):
        full_path = path.realpath(log_path)
        base = path.basename(full_path)
        parent_dir = path.basename(path.dirname(full_path))

        result = FrameTimeResult()
        result.path = full_path
        result.run_name = parent_dir + '/' + base

        with open(full_path) as csvfile:
            log_file = csv.reader(csvfile)
            for i, row in enumerate(log_file):
                if i == 0:
                    continue
                assert len(row) == 1
                result.raw_frametimes.append(int(row[0]))

        nannos_per_millis = 10**6
        result.total_duration_ms = np.sum(result.raw_frametimes) / nannos_per_millis
        result.average_frametime_ms = np.average(result.raw_frametimes) / nannos_per_millis
        result.percentile_frametime_ms = [np.percentile(result.raw_frametimes, p) / nannos_per_millis for p in range(100)]
        return result

    def p50(self):
        assert len(self.percentile_frametime_ms) == 100
        return self.percentile_frametime_ms[50]

    def p90(self):
        assert len(self.percentile_frametime_ms) == 100
        return self.percentile_frametime_ms[90]

    def p95(self):
        assert len(self.percentile_frametime_ms) == 100
        return self.percentile_frametime_ms[95]

    def median(self):
        return self.p50()

    def percent_missed(self):
        '''Returns the percent of frames that lasted more than the target frametime'''
        return np.average([1 if x > self.TargetFrameTime else 0 for x in self.raw_frametimes]) * 100

    def missed_score(self):
        '''
        Missed score for a single frame is the square of the time it took *over* the target frametime.
        For example, a frame time of 20ms has the missed score of (20ms-16ms)^2 assuming the target frame time of 16ms.
        This returns the average score of the whole run. This will most likely be a large number.
        '''
        return np.average([(x - self.TargetFrameTime)**2 if x > self.TargetFrameTime else 0 for x in self.raw_frametimes])

    def dump(self):
        print(f'{self.run_name}:\tduration: {self.total_duration_ms:.3f} ms,\taverage: {self.average_frametime_ms:.3f} ms')
        print(f'\t\tmedian: {self.median():.3f} ms,\tp90: {self.p90():.3f} ms,\t\t\tp95: {self.p95():.3f} ms')
        print(f'\t\tmissed frames: {self.percent_missed():.3f}%,\tmissed score: {self.missed_score():.2e}')


def main():
    parser = argparse.ArgumentParser(description='Process and analyze series of frame time logs.')
    parser.add_argument('--dataset', type=str, nargs='+', action='append', help='Dataset name followed by a list of log files: dataset_name log_file+')
    parser.add_argument('-o', '--output', type=str, default='combined.png', help='Output file name')
    parser.add_argument('-v', '--verbose', type=bool, default=False, help='Output gif file name')
    args = parser.parse_args()

    datasets = args.dataset
    output_file = args.output
    verbose = args.verbose

    outputs = []
    selected_results = []

    for dataset in datasets:
        dataset_name = dataset[0]
        print(f'~~~~ Processing dataset {dataset_name} ~~~~')
        results = []
        for file in dataset[1:]:
            results.append(FrameTimeResult.from_file(file))
            if verbose:
                results[-1].dump()
                print()

        print(f'Dataset size: {len(results)}')
        sorted_results = sorted(results, key=lambda x : x.percentile_frametime_ms[50])
        considered_results = sorted_results[1:-1]
        if len(considered_results) == 0:
            considered_results = sorted_results

        for (metric, fn) in [('Min', np.min), ('Max', np.max), ('Standard deviation', np.std)]:
            avg_agg = fn([x.average_frametime_ms for x in considered_results])
            median_agg = fn([x.median() for x in considered_results])
            p90_agg = fn([x.p90() for x in considered_results])
            p95_agg = fn([x.p95() for x in considered_results])
            missed_agg = fn([x.percent_missed() for x in considered_results])
            missed_score_agg = fn([x.missed_score() for x in considered_results])
            print(f'{metric}:\n\tavg: {avg_agg:.3f}\n\tmedian: {median_agg:.3f}\n\tp90: {p90_agg:.3f}\n\tp95: {p95_agg:.3f}')
            print(f'\tmissed frames: {missed_agg:.3f} %\n\tmissed score: {missed_score_agg:.2e}\n')

        median_res = sorted_results[len(sorted_results) // 2]
        print(f'Median result for {dataset_name}:')
        median_res.dump()
        outfile = dataset_name + '_frametimes.png'
        outputs.append((dataset_name, outfile))
        selected_results.append((sorted_results[0], median_res))
        args = [plot_script_path, sorted_results[0].path, median_res.path]
        subprocess.run(args, env={'OUTFILE': outfile})
        print(f'Plot saved as {outfile}')
        print('---------------------------------------------------------------------\n')

    final_plot_args = [plot_script_path]
    for res in selected_results:
        final_plot_args.append(res[0].path)
        if res[0] != res[1]:
            final_plot_args.append(res[1].path)

    outfile_no_ext = path.splitext(output_file)[0]
    final_outfile = outfile_no_ext + '.png'
    subprocess.run(final_plot_args, env={'OUTFILE': final_outfile})
    print(f'Final plot saved as {final_outfile}')


if __name__ == '__main__':
    main()
