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
    NonosPerMilli = 10**6
    MillisPerSecond = 10**3
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
    def from_file(log_path, gameplay_state=None, gameplay_duration=None, drop_first_seconds=None):
        """
        Creates a FrameTimeResult from a file.
        - if |gameplay_state| is specified, all frame time statistics will include frames only in that state;
        - if |gameplay_duration| is specified, all frame times after the specified duration (in seconds) will be discarded;
        - if |drop_first_seconds| is specified, the specified number of initial benchmark seconds will be discarded.
        """
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

                frametime_nanos = int(row[0])
                frame_state = int(row[1])
                seen_states.add(frame_state)
                if frame_state not in result.state_to_duration_ms:
                    result.state_to_duration_ms[frame_state] = 0

                result.state_to_duration_ms[frame_state] += frametime_nanos / result.NonosPerMilli
                if gameplay_state is not None and gameplay_state != frame_state:
                    continue

                result.raw_frametimes.append(frametime_nanos)
                result.frame_states.append(frame_state)

        if drop_first_seconds is not None:
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

        if gameplay_duration is not None:
            target_duration_nanos = gameplay_duration * result.NanosPerSecond
            curr_duration = 0
            first_frame_to_discard = 0
            for frametime in result.raw_frametimes:
                curr_duration += frametime
                first_frame_to_discard += 1
                if curr_duration > target_duration_nanos:
                    break

            result.raw_frametimes = result.raw_frametimes[:first_frame_to_discard]
            result.frame_states = result.frame_states[:first_frame_to_discard]

        result.total_duration_ms = np.sum(result.raw_frametimes) / result.NonosPerMilli
        result.average_frametime_ms = np.average(result.raw_frametimes) / result.NonosPerMilli
        result.percentile_frametime_ms = \
            [np.percentile(result.raw_frametimes, p) / result.NonosPerMilli for p in range(100)]

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
        Returns the total number of seconds spent in |state_idx|.
        If no frame time was spent in a given state, returns 0.
        '''
        if state_idx in self.state_to_duration_ms:
            return self.state_to_duration_ms[state_idx] / self.MillisPerSecond
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
            print(f'\t\ttime in state {state}: {ms:.3f} s')


FrameTimesMetrics = namedtuple('FrameTimesMetrics',
                               ['avg', 'median', 'p90', 'p95',
                                'missed_percent', 'init_time'])

class SummaryKind(Enum):
    ABSOLUTE = 1
    RELATIVE = 2

FrameTimesSummaryFn = namedtuple('FrameTimesSummaryFn', ['name', 'fn', 'kind'])

def format_summarized_value(summary_fn, value):
    value_str = f'{value:.3f}' if value < 10**6 else f'{value:.2e}'
    return value_str if summary_fn.kind == SummaryKind.ABSOLUTE else value_str + '%'

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

    metric_to_summaries = {}
    for summary_fn in summary_fns:
        avg = summary_fn.fn([x.average_frametime_ms for x in results])
        median = summary_fn.fn([x.median() for x in results])
        p90 = summary_fn.fn([x.p90() for x in results])
        p95 = summary_fn.fn([x.p95() for x in results])
        missed_percent = summary_fn.fn([x.percent_missed() for x in results])
        init_time = summary_fn.fn([x.time_in_state(0) for x in results])

        metrics = FrameTimesMetrics(avg, median, p90, p95, missed_percent, init_time)
        for metric_value, metric_name in zip(metrics, metrics._fields):
            if metric_name not in metric_to_summaries:
                metric_to_summaries[metric_name] = []
            metric_to_summaries[metric_name].append((summary_fn, metric_value))

    return metric_to_summaries

def relative_noise(data):
    min_res = data_low(data)
    max_res = data_high(data)
    return (abs(min_res - max_res) / min_res) * 100


def main():
    parser = argparse.ArgumentParser(description='Process and analyze series of frame time logs.')
    parser.add_argument('--dataset', type=str, nargs='+', action='append', help='Dataset name followed by a list of log files: dataset_name log_file+')
    parser.add_argument('--duration', type=int, default=None, help='Number of seconds of gameplay data to analyze')
    parser.add_argument('--drop_front', type=int, default=None, help='Number of seconds of the initial data to ignore')
    parser.add_argument('--gameplay_state', type=int, default=None, help='The only state number to consider when calculating frame times statistics')
    parser.add_argument('--print_csv', type=bool, default=False, help='Prints stats as comma separated values (CSV)')
    parser.add_argument('-v', '--verbose', type=bool, default=False, help='Output gif file name')
    args = parser.parse_args()

    datasets = args.dataset
    gameplay_duration = args.duration
    drop_front = args.drop_front
    gameplay_state = args.gameplay_state
    use_csv = args.print_csv
    verbose = args.verbose

    separator = ',' if use_csv else '\t\t'

    for dataset in datasets:
        dataset_name = dataset[0]
        print(f'~~~~ Processing dataset {dataset_name} ~~~~\n')
        results = []
        for file in dataset[1:]:
            results.append(FrameTimeResult.from_file(file, gameplay_state, gameplay_duration, drop_front))
            if verbose:
                results[-1].dump()
                print()

        print(f'Dataset: {dataset_name}{separator}size: {len(results)}')
        summary_fns = []
        summary_fns += [FrameTimesSummaryFn('P5', data_low, SummaryKind.ABSOLUTE)]
        summary_fns += [FrameTimesSummaryFn('Median', np.median, SummaryKind.ABSOLUTE)]
        summary_fns += [FrameTimesSummaryFn('P95', data_high, SummaryKind.ABSOLUTE)]
        summary_fns += [FrameTimesSummaryFn('Std Dev', np.std, SummaryKind.ABSOLUTE)]
        summary_fns += [FrameTimesSummaryFn('Noise', relative_noise, SummaryKind.RELATIVE)]

        results_header = 'Metric'
        for summary_fn in summary_fns:
            results_header = results_header + separator + summary_fn.name
        print(results_header)

        for metric, summaries in summarize_frame_times(results, summary_fns).items():
            summarized_metric_str = metric
            for summary_fn, metric_value in summaries:
                summarized_metric_str = summarized_metric_str + separator + format_summarized_value(summary_fn, metric_value)
            print(summarized_metric_str)

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
