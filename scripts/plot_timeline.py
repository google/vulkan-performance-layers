#!/usr/bin/env python3
# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""
A script for plotting event log files.

Sample use:
    plot_timeline.py \
        --dataset A /my/path/A/events.log \
        --dataset B /my/path/B/events.log \
        -o timeline.png

To see full command line options documentation, run:
  plot_timeline.py --help
"""

import re
import pandas as pd
import sys
import numpy as np
import matplotlib.pyplot as plt
import argparse
import csv
import matplotlib
matplotlib.use('Agg')  # Allows to work without X dispaly

nanos_per_second = 10**9
nanos_per_milli = 10**6


class FrameEvent:
    """
    A datastructure containing Frame event data

    A frame event starts with `frame_present` and contains frame_time
    and the frame state information.
    """

    def __init__(self, timestamp: int, event_str: str):
        self.timestamp = timestamp

        pattern = r',frame_time:(\d+),started:([0-1])'
        attributes = re.match(pattern, event_str)
        assert (attributes)
        attributes = attributes.groups()
        assert (len(attributes) >= 2)

        self.frame_time = int(attributes[0])
        self.started = int(attributes[1])


class PipelineEvent:
    """
    A datastructure containing Pipline event data.

    A Pipeline event starts with `create_graphics_pipelines` or
    `create_compute_pipelines` and contains the shader hashes and pipline duration.
    """

    def __init__(self, timestamp: int, event_str: str):
        self.timestamp = timestamp

        pattern = r',hashes:"\[((0x[a-f0-9]*)+,*)+\]",duration:(\d+)'
        attributes = re.match(pattern, event_str)
        assert (attributes)
        attributes = attributes.groups()
        assert (len(attributes) >= 2)

        self.hashes = attributes[:-1]
        self.duration = int(attributes[-1])


def get_frames_per_second(frame_present_events):
    """
    Buckets |frame_present_events| by seconds. Returns 3 equal-length arrays,
    (xs, ys, states). For each second xs[i], ys[i] represents frames elapsed in
    that second, and states[i] state in that second.
    """
    num_events = len(frame_present_events)
    df = pd.DataFrame({'V': [e.started for e in frame_present_events]}, index=[
                      pd.Timedelta(e.timestamp) for e in frame_present_events])
    fps = list(df.rolling('1s').count()['V'])
    assert len(fps) == num_events

    xs = np.zeros(num_events)
    ys = np.zeros(num_events)
    states = np.zeros(num_events, dtype=np.int32)

    for i, event in enumerate(frame_present_events):
        timepoint, _frame_time, state = event.timestamp, event.frame_time, event.started
        elapsed_seconds = timepoint / nanos_per_second
        xs[i] = elapsed_seconds
        ys[i] = fps[i]
        states[i] = state

    return xs, ys, states


def split_by_state(xs, ys, states):
    """
    Splits the results get_frame_per_second into a list of continuous line segments,
    divided by state. This is to plot multiple line segments with different color for
    each segment.
    """
    res = []
    last_state = None
    for x, y, s in zip(xs, ys, states):
        if s != last_state:
            res.append((s, [], []))
            last_state = s

        res[-1][1].append(x)
        res[-1][2].append(y)

    return res


def get_pipeline_creation_times(create_pipeline_events):
    """
    Returns a pair of equal-length arrays that map times of pipeline creations
    (in seconds) to the time it took to create them (in milliseconds).
    """
    xs = []
    ys = []

    for event in create_pipeline_events:
        elapsed_seconds = event.timestamp / nanos_per_second
        xs.append(elapsed_seconds)
        ys.append(event.duration / nanos_per_milli)

    return xs, ys


def plot_frames_per_second(ax, events_by_type):
    ax.set_xlabel('Time Since Start [s]')
    ax.set_ylabel('Frames Per Second')

    if 'frame_present' not in events_by_type:
        return

    fps_x, fps_y, states = get_frames_per_second(events_by_type['frame_present'])
    for state, xs, ys in split_by_state(fps_x, fps_y, states):
        ax.plot(xs, ys, color=plt.cm.tab10(state), label=f'FPS in state {state}')

    ax.legend(loc='upper right')
    ax.grid()


def plot_pipeline_creations(ax, events_by_type):
    ax.set_ylabel('Creation Time [ms]')
    max_creation_time_millis = 0

    if 'create_graphics_pipelines' in events_by_type:
        xs, ys = get_pipeline_creation_times(events_by_type['create_graphics_pipelines'])
        ax.scatter(xs, ys, s=3, color=plt.cm.tab10(8), label='Create Graphics Pipelines')
        max_creation_time_millis = max(max_creation_time_millis, max(ys))

    if 'create_compute_pipelines' in events_by_type:
        xs, ys = get_pipeline_creation_times(events_by_type['create_compute_pipelines'])
        ax.scatter(xs, ys, s=3, color=plt.cm.tab10(9), label='Create Compute Pipelines')
        max_creation_time_millis = max(max_creation_time_millis, max(ys))

    ax.legend(loc='lower right')
    return max_creation_time_millis


def parse_event_log(log: str) -> (str, int, str):
    """
    Returns a triple containing event name, timestamp, and rest of event which
    may be empty or contain attributes.
    log format sample:
    sample1: `compile_time_layer_init,timestamp:123`
    sample2: `frame_present,timestamp:1667942408738000395,frame_time:9707270,started:1`
    """
    # This pattern captures `event_type`, `timestamp`, and the rest of the event.
    pattern = r'(\w+),timestamp:(\d+)(.*)'
    result = re.match(pattern, log).groups()
    assert (len(result) >= 2)
    event_type = result[0]
    timestamp = int(result[1])
    attributes = result[2]
    return event_type, timestamp, attributes


def main():
    parser = argparse.ArgumentParser(description='Processed an event log file and outputs a timelien of events')
    parser.add_argument('-d', '--dataset', type=str, nargs=2, action='append', help='Dataset name followed by an event log file')
    parser.add_argument('-o', '--output', type=str, default='timeline.png', help='Output timeline filename (.png, .svg, .pdf)')
    args = parser.parse_args()

    output_file_path = args.output

    if args.dataset is None:
        print('No dataset provided', file=sys.stderr)
        exit(2)

    num_datasets = len(args.dataset)
    fig, axs = plt.subplots(num_datasets, 1, figsize=(12, 4 * num_datasets), constrained_layout=True)
    if num_datasets == 1:
        axs = [axs]
    right_axs = [ax.twinx() for ax in axs]
    fig.suptitle(f'Timeline View')

    max_duration_seconds = 0
    max_creation_time_millis = 0

    for dataset_idx, (dataset_name, eventlog_filename) in enumerate(args.dataset):
        start_timestamp = None
        duration_nanos = 0
        events_by_type = {}

        with open(eventlog_filename) as input_file:
            for i, row in enumerate(input_file):
                event_type, event_timestamp, event_attributes = parse_event_log(row)
                if i == 0:
                    start_timestamp = event_timestamp
                if event_type not in events_by_type:
                    events_by_type[event_type] = []

                nanos_since_start = event_timestamp - start_timestamp
                duration_nanos = max(nanos_since_start, duration_nanos)
                if event_type == 'frame_present':
                    events_by_type[event_type].append(FrameEvent(nanos_since_start, event_attributes))
                elif event_type == 'create_graphics_pipelines' or event_type == 'create_compute_pipelines':
                    events_by_type[event_type].append(PipelineEvent(nanos_since_start, event_attributes))
                else:
                    events_by_type[event_type].append((nanos_since_start,) + (event_attributes,))

        duration_seconds = duration_nanos / nanos_per_second
        max_duration_seconds = max(max_duration_seconds, duration_seconds)
        fps_ax = axs[dataset_idx]
        fps_ax.set_title(f'Dataset {dataset_name}')

        plot_frames_per_second(fps_ax, events_by_type)

        creation_times_ax = right_axs[dataset_idx]
        curr_max_creation_time = plot_pipeline_creations(creation_times_ax, events_by_type)
        max_creation_time_millis = max(max_creation_time_millis, curr_max_creation_time)

    # Set all subplots have the same scale to make all plots align horizontally and vertically.
    for fps_ax, creation_ax in zip(axs, right_axs):
        max_fps = 100
        fps_ax.set_ylim([0, max_fps])
        fps_ax.set_yticks(np.arange(0, max_fps, 5))
        fps_ax.set_xlim([0, max_duration_seconds])
        fps_ax.set_xticks(np.arange(0, max_duration_seconds, 10))
        creation_ax.set_ylim([0, max_creation_time_millis + 100])
        creation_ax.set_yticks(np.arange(0, max_creation_time_millis + 100, 200))

    fig.savefig(output_file_path, dpi=300)


if __name__ == '__main__':
    main()
