#!/usr/bin/env python3
"""Capture repeated HH-CG2033A IR frames via Nature Remo's local API.

Raw captures and generated signal data belong in a Git-ignored directory.
Does not emit any IR signal.
"""
import argparse
import json
from pathlib import Path
import re
import time
import urllib.error
import urllib.request


def extract(raw):
    text = raw.decode('utf-8', errors='strict')
    frequency = re.search(r'"freq"\s*:\s*(\d+)', text)
    data_text = re.search(r'"data"\s*:\s*\[([^\]]*)', text)
    if not frequency or not data_text:
        return None
    # Ignore an unfinished final integer in truncated responses.
    values = [int(v) for v in re.findall(r'(\d+)\s*[,\]]', data_text.group(1) + (']' if ']' in text else ''))]
    frames = []
    for i in range(len(values) - 82):
        frame = values[i:i + 83]
        if not (2500 <= frame[0] <= 4500 and 1000 <= frame[1] <= 2500):
            continue
        if not all(200 <= v <= 700 for v in frame[2::2]):
            continue
        if not all(200 <= v <= 2000 for v in frame[3::2]):
            continue
        bits = ''.join('1' if v > 900 else '0' for v in frame[3::2])
        frames.append((i, frame, bits))
    pair = None
    for first, second in zip(frames, frames[1:]):
        start, frame, bits = first
        gap = values[start + 83:second[0]]
        if bits == second[2] and len(gap) in (1, 3) and 40000 <= sum(gap) <= 120000:
            pair = frame, gap, bits
            break
    if pair is None:
        return None
    frame, gap, bits = pair
    data = []
    for n in range(5):
        if n:
            data.extend(gap)
        data.extend(frame)
    message = {'format': 'us', 'freq': int(frequency.group(1)), 'data': data}
    if len(json.dumps(message, separators=(',', ':')).encode()) > 2048:
        return None
    return bits, message


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--name', required=True)
    parser.add_argument('--directory', default='.local/remo-ir')
    parser.add_argument('--wait', type=int, default=60)
    args = parser.parse_args()
    directory = Path(args.directory)
    directory.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    def receive():
        request = urllib.request.Request('http://' + args.host + '/messages',
                                         headers={'X-Requested-With': 'XMLHttpRequest'})
        with opener.open(request, timeout=2) as response:
            return response.read()
    try:
        initial = extract(receive())
    except (OSError, ValueError):
        initial = None
    print('Waiting for a different repeated IR command.', flush=True)
    deadline = time.monotonic() + args.wait
    while time.monotonic() < deadline:
        try:
            raw = receive()
            result = extract(raw)
            if result and (initial is None or result[0] != initial[0]):
                bits, message = result
                (directory / (args.name + '-raw.txt')).write_bytes(raw)
                (directory / (args.name + '-5frames.json')).write_text(json.dumps(message, separators=(',', ':')))
                print('Captured', args.name, 'command', bits, 'duration_ms', round(sum(message['data']) / 1000), flush=True)
                return
        except (OSError, ValueError):
            pass
        time.sleep(0.3)
    raise SystemExit('No new repeated command captured within the time window.')


if __name__ == '__main__':
    main()
