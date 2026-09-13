#!/usr/bin/env python3
"""Generate PlatformIO build flags from .env file.

Reads FLOCKYOU_WIFI_SSID and FLOCKYOU_WIFI_PASS from .env and outputs
-D define flags. This keeps credentials out of source code and
version control.

Usage in platformio.ini:
    build_flags =
        !python generate_build_flags.py

The output wraps each string value in single quotes so the shell
passes them through intact to PlatformIO's build system, producing
proper C string-literal defines like -DFY_WS_SSID="flock-you".
"""

import os
import re

def parse_env(path):
    """Parse a simple KEY=value .env file, ignoring comments and blanks."""
    values = {}
    if not os.path.exists(path):
        return values
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)=(.*)$', line)
            if m:
                key = m.group(1)
                val = m.group(2).strip()
                if (val.startswith('"') and val.endswith('"')) or \
                   (val.startswith("'") and val.endswith("'")):
                    val = val[1:-1]
                values[key] = val
    return values

def main():
    env_path = os.path.join(os.path.dirname(__file__), '.env')
    env = parse_env(env_path)

    ssid = env.get('FLOCKYOU_WIFI_SSID', '')
    password = env.get('FLOCKYOU_WIFI_PASS', '')

    flags = []
    if ssid:
        # Single-quote the value so the shell passes the double quotes through
        # to the compiler as part of the -D flag
        flags.append(f"-DFY_WS_SSID='\"{ssid}\"'")
    if password:
        flags.append(f'-DFY_WS_PASS=\'"{password}"\'')
        flags.append(f'-DFY_WS_PASS_LEN={len(password)}')

    print(' '.join(flags))

if __name__ == '__main__':
    main()
