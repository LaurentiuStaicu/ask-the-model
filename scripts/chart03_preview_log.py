#!/usr/bin/env python3
"""Bounded CI preview transport for text-only review clients; no runtime role."""
import base64
import hashlib
from pathlib import Path
import sys

root = Path(sys.argv[1])
for name in ('atm-chart-line.png', 'atm-chart-data.tsv', 'atm-chart-scatter.png', 'atm-chart-small.png'):
    data = (root / name).read_bytes()
    if not 0 < len(data) <= 256 * 1024:
        raise ValueError(f'Preview size outside diagnostic bound: {name}')
    encoded = base64.b64encode(data).decode('ascii')
    for index, start in enumerate(range(0, len(encoded), 1500)):
        print(f'CHART03-PREVIEW {name} {index} {encoded[start:start+1500]}')
    print(f'CHART03-END {name} {len(data)} {hashlib.sha256(data).hexdigest()}')
