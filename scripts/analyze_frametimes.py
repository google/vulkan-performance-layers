#!/usr/bin/env python3

"""
A script for analyzing and comparing frame time layer logs.

Sample use:
    analyze_frametimes.py \
        --dataset baseline /my/path/baseline/**/frame_times.log \
        --dataset test /my/path/test/**/frame_times.log

In a addition to stats printed to stdout, the above command produces CSV files with
Frames per Second over time.
"""

import argparse
import csv
import subprocess
import sys
import os.path as path
import numpy as np
import tempfile

from collections import namedtuple as namedtuple
from enum import Enum as Enum


class FrameTimeResult(object):
    '''
    Represents frametimes from a single frame time layer log.
    '''
    NanosPerSecond = 10**9
    TargetFPS = 45
    TargetFrameTime = NanosPerSecond / TargetFPS

    def __init__(self):
        self.path = ""
        self.run_name = ""
        self.raw_frametimes = []
        self.frame_states = []
        self.total_duration_ms = -1
        self.average_frametime_ms = -1
        self.percentile_frametime_ms = []
        self.state_to_duration_ms = {}

    @staticmethod
    def from_file(log_path, drop_first_seconds=None):
        full_path = path.realpath(log_path)
        base = path.basename(full_path)
        parent_dir = path.basename(path.dirname(full_path))

        result = FrameTimeResult()
        result.path = full_path
        result.run_name = parent_dir + '/' + base
        seen_states = set()

        with open(full_path) as csvfile:
            log_file = csv.reader(csvfile)
            for i, row in enumerate(log_file):
                if i == 0:
                    continue
                assert len(row) == 2
                result.raw_frametimes.append(int(row[0]))
                result.frame_states.append(int(row[1]))
                seen_states.add(int(row[1]))

        if drop_first_seconds is not None and drop_first_seconds > 0:
            drop_first_nanos = drop_first_seconds * result.NanosPerSecond
            curr_duration = 0
            drop_end = 0
            for frametime in result.raw_frametimes:
                curr_duration += frametime
                if curr_duration >= drop_first_nanos:
                    break

                drop_end += 1

            result.raw_frametimes = result.raw_frametimes[drop_end:]
            result.frame_states = result.frame_states[drop_end:]

        nanos_per_millis = 10**6
        result.total_duration_ms = np.sum(result.raw_frametimes) / nanos_per_millis
        result.average_frametime_ms = np.average(result.raw_frametimes) / nanos_per_millis
        result.percentile_frametime_ms = [np.percentile(result.raw_frametimes, p) / nanos_per_millis for p in range(100)]

        for state in sorted(seen_states):
            nanos_in_state = 0
            for ft_ns, frame_state in zip(result.raw_frametimes, result.frame_states):
                if frame_state == state:
                    nanos_in_state += ft_ns

            result.state_to_duration_ms[state] = nanos_in_state / nanos_per_millis

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

    def time_in_state(self, state_idx):
        '''
        Returns the total number of milliseconds spent in |state_idx|.
        If no frame time was spent in a given state, returns 0.
        '''
        if state_idx in self.state_to_duration_ms:
            return self.state_to_duration_ms[state_idx]
        return 0

    def fps_over_time(self):
        duration_s = int(self.total_duration_ms / 1000) + 1
        bins = [(0, 0)] * duration_s
        curr_duration_ns = 0

        assert len(self.raw_frametimes) == len(self.frame_states)
        for frame_time, frame_state in zip(self.raw_frametimes, self.frame_states):
            curr_duration_ns += frame_time
            bin_idx = int(curr_duration_ns / self.NanosPerSecond)
            # Arbitrarily decide to keep the latest frame state in the current bin.
            # Instead, we could also get the longest state within this state, first one,
            # or decide some other way.
            bins[bin_idx] = (bins[bin_idx][0] + 1, frame_state)

        return bins

    def dump(self):
        print(f'{self.run_name}:\tduration: {self.total_duration_ms:.3f} ms,\taverage: {self.average_frametime_ms:.3f} ms')
        print(f'\t\tmedian: {self.median():.3f} ms,\tp90: {self.p90():.3f} ms,\t\t\tp95: {self.p95():.3f} ms')
        print(f'\t\tmissed frames: {self.percent_missed():.3f}%')
        for state, ms in self.state_to_duration_ms.items():
            print(f'\t\ttime in state {state}: {ms:.3f} ms')


FrameTimesMetrics = namedtuple('FrameTimesMetrics',
                               ['avg', 'median', 'p90', 'p95',
                                'missed_percent', 'init_time'])

class SummaryKind(Enum):
    ABSOLUTE = 1
    RELATIVE = 2

FrameTimesSummaryFn = namedtuple('FrameTimesSummaryFn', ['name', 'fn', 'kind'])
FrameTimesSummary = namedtuple('FrameTimesSummary', ['summary_fn', 'metrics'])

def print_frame_times_summary(summary, as_csv=False):
    if not as_csv:
        print(f'{summary.summary_fn.name}:')

    for k, v in zip(summary.metrics._fields, summary.metrics):
        value_str = f'{v:.3f}' if v < 10**6 else f'{v:.2e}'
        if summary.summary_fn.kind == SummaryKind.RELATIVE:
            value_str = value_str + '%'

        if not as_csv:
            print(f'\t\t{k}: {value_str}')
        else:
            print(f'{summary.summary_fn.name},{k},{value_str}')

def data_low(data):
    return np.percentile(data, 5)

def data_high(data):
    return np.percentile(data, 95)

def summarize_frame_times(results, summary_fns):
    sorted_results = sorted(results, key=lambda x: x.median())
    medians = [x.median() for x in sorted_results]
    low_median = data_low(medians)
    high_median = data_high(medians)
    considered_results = list(filter(lambda x: low_median < x.median() < high_median, sorted_results))
    if len(considered_results) == 0:
        considered_results = sorted_results

    summaries = []
    for summary_fn in summary_fns:
        avg = summary_fn.fn([x.average_frametime_ms for x in results])
        median = summary_fn.fn([x.median() for x in results])
        p90 = summary_fn.fn([x.p90() for x in results])
        p95 = summary_fn.fn([x.p95() for x in results])
        missed_percent = summary_fn.fn([x.percent_missed() for x in results])
        init_time = summary_fn.fn([x.time_in_state(0) for x in results])
        metrics = FrameTimesMetrics(avg, median, p90, p95, missed_percent, init_time)
        summaries.append(FrameTimesSummary(summary_fn, metrics))

    return summaries

def relative_noise(data):
    min_res = data_low(data)
    max_res = data_high(data)
    return (abs(min_res - max_res) / min_res) * 100


def main():
    parser = argparse.ArgumentParser(description='Process and analyze series of frame time logs.')
    parser.add_argument('--dataset', type=str, nargs='+', action='append', help='Dataset name followed by a list of log files: dataset_name log_file+')
    parser.add_argument('-c', '--cutoff', type=int, default=None, help='Number of seconds of the initial data to ignore')
    parser.add_argument('--print_csv', type=bool, default=False, help='Prints stats as comma separated values (CSV)')
    parser.add_argument('-v', '--verbose', type=bool, default=False, help='Output gif file name')
    args = parser.parse_args()

    datasets = args.dataset
    cutoff = args.cutoff
    use_csv = args.print_csv
    verbose = args.verbose

    for dataset in datasets:
        dataset_name = dataset[0]
        print(f'~~~~ Processing dataset {dataset_name} ~~~~')
        results = []
        for file in dataset[1:]:
            results.append(FrameTimeResult.from_file(file, cutoff))
            if verbose:
                results[-1].dump()
                print()

        print(f'Dataset size: {len(results)}')

        summary_fns = []
        summary_fns += [FrameTimesSummaryFn('Low (p5)', data_low, SummaryKind.ABSOLUTE)]
        summary_fns += [FrameTimesSummaryFn('Median', np.median, SummaryKind.ABSOLUTE)]
        summary_fns += [FrameTimesSummaryFn('High (p95)', data_high, SummaryKind.ABSOLUTE)]
        summary_fns += [FrameTimesSummaryFn('Standard deviation', np.std, SummaryKind.ABSOLUTE)]
        summary_fns += [FrameTimesSummaryFn('Noise', relative_noise, SummaryKind.RELATIVE)]

        for summary in summarize_frame_times(results, summary_fns):
            print_frame_times_summary(summary, as_csv=use_csv)
            print()

        sorted_results = sorted(results, key=lambda x: x.median())
        median_res = sorted_results[len(sorted_results) // 2]
        print(f'Median result for {dataset_name}:')
        median_res.dump()
        print('---------------------------------------------------------------------\n')

        low_p5_idx = int(len(sorted_results) * 0.05)
        high_p95_idx = int(len(sorted_results) * 0.95)

        p5_fps_over_time = sorted_results[low_p5_idx].fps_over_time()
        median_fps_over_time = median_res.fps_over_time()
        p95_fps_over_time = sorted_results[high_p95_idx].fps_over_time()
        with open(dataset_name + '_fps.csv', 'w') as fps_file:
            fps_csv = csv.writer(fps_file)
            fps_csv.writerow(('Second', 'Low FPS', 'Low State',
                              'Median FPS', 'Median State',
                              'High FPS', 'High State'))
            for i, (x, y, z) in enumerate(zip(p5_fps_over_time, median_fps_over_time, p95_fps_over_time)):
                fps_csv.writerow((i, x[0], x[1], y[0], y[1], z[0], z[1]))
            print('Fps saved as: ' + dataset_name + '_fps.csv')
        print('---------------------------------------------------------------------\n')

if __name__ == '__main__':
    main()
