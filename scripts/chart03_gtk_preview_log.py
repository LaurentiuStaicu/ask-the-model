#!/usr/bin/env python3
"""Bounded native GTK screenshot transport; test diagnostics only."""
import base64
import hashlib
from pathlib import Path
import sys
root = Path(sys.argv[1])
for name in ('atm-gtk-chart.png', 'atm-gtk-data.png', 'atm-gtk-small.png'):
    data = (root / name).read_bytes()
    if not 0 < len(data) <= 256 * 1024:
        raise ValueError(f'GTK preview exceeds diagnostic bounds: {name}')
    encoded = base64.b64encode(data).decode('ascii')
    for index, start in enumerate(range(0, len(encoded), 1500)):
        print(f'CHART03-GTK {name} {index} {encoded[start:start+1500]}')
    print(f'CHART03-GTK-END {name} {len(data)} {hashlib.sha256(data).hexdigest()}')
