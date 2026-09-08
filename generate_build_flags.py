#!/usr/bin/env python3
# Generate PlatformIO build_flags from .env for compile-time WiFi credentials

import os, sys

env_path = os.path.join(os.path.dirname(__file__), '.env')
if not os.path.exists(env_path):
    print("-DFY_WS_SSID=\\\"flock-you\\\" -DFY_WS_PASS=\\\"flockyou\\\"")
    sys.exit(0)

flags = []
with open(env_path) as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        if '=' in line:
            k, v = line.split('=', 1)
            k = k.strip()
            v = v.strip().strip('"\'')
            if k == 'FLOCKYOU_WIFI_SSID':
                flags.append(f'-DFY_WS_SSID=\\\"{v}\\\"')
            elif k == 'FLOCKYOU_WIFI_PASS':
                flags.append(f'-DFY_WS_PASS=\\\"{v}\\\"')

if not flags:
    flags = ['-DFY_WS_SSID=\\\"flock-you\\\"', '-DFY_WS_PASS=\\\"flockyou\\\"']

print(' '.join(flags))
