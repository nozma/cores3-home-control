#!/usr/bin/env python3
"""Generate an ignored firmware header from captured five-frame IR bursts."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--directory', default='.local/remo-ir')
    parser.add_argument('--output', default='include/remo_local.h')
    args = parser.parse_args()
    if not args.host or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-' for c in args.host):
        raise SystemExit('Use a LAN IPv4 address or hostname without a scheme or port.')
    sources = {'UP': 'brighten-5frames.json', 'DOWN': 'dim-hold-5frames.json',
               'WARM': 'warm-5frames.json', 'COOL': 'cool-5frames.json'}
    lines = ['#pragma once', '// Generated locally. Keep outside Git.',
             'constexpr char REMO_LOCAL_HOST[] = ' + json.dumps(args.host) + ';']
    for name, filename in sources.items():
        message = json.loads((Path(args.directory) / filename).read_text())
        values = message['data']
        if (message['format'] != 'us' or not 30 <= message['freq'] <= 80 or
                not values or len(values) % 2 != 1 or
                not all(type(v) is int and 0 <= v <= 65535 for v in values) or
                not 0 < sum(values) <= 1000000):
            raise SystemExit('Invalid or excessively long signal: ' + filename)
        payload = json.dumps(message, separators=(',', ':'))
        if len(payload.encode()) > 2048:
            raise SystemExit('Signal payload exceeds the tested size: ' + filename)
        lines.append('constexpr char REMO_LOCAL_' + name + '[] = R"IR(' + payload + ')IR";')
        lines.append('constexpr unsigned REMO_LOCAL_' + name + '_MS = ' + str((sum(values) + 999) // 1000) + ';')
    Path(args.output).write_text('\n'.join(lines) + '\n')
    print('Generated four local IR bursts. Host and payloads were not printed.')


if __name__ == '__main__':
    main()
