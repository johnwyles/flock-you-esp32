#!/bin/bash
# gpg-login-preset.sh — Cache GPG passphrase in gpg-agent at login
#
# This script retrieves the GPG passphrase from the GNOME keyring
# (stored under the "login" keyring with the application name "gpg-passphrase")
# and presets it in gpg-agent so signed commits don't require manual entry.
#
# Install:
#   1. Store your GPG passphrase in the GNOME keyring once:
#      secret-tool store --label="GPG Passphrase" application gpg-passphrase
#      (you will be prompted for your GPG passphrase)
#   2. Add this script to ~/.xprofile or your desktop session's autostart:
#      echo '~/.local/bin/gpg-login-preset.sh &' >> ~/.xprofile
#
# Requirements:
#   - libsecret-tools (for secret-tool) OR python3 with secretstorage
#   - gpg-preset-passphrase (from gnupg2 package)

KEYID="B4B401B61F522B0AD4468897A5CDA53D223A2284"

# Try to retrieve passphrase from GNOME keyring
if command -v secret-tool &>/dev/null; then
    PASS=$(secret-tool lookup application gpg-passphrase 2>/dev/null)
elif python3 -c "import secretstorage" 2>/dev/null; then
    PASS=$(python3 -c "
import secretstorage
conn = secretstorage.connect()
items = conn.search_items({'application': 'gpg-passphrase'})
for item in items:
    print(item.get_secret().decode())
    break
conn.close()
" 2>/dev/null)
fi

if [ -n "$PASS" ]; then
    /usr/lib/gnupg2/gpg-preset-passphrase --preset "$KEYID" <<< "$PASS" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "[gpg-login-preset] GPG passphrase cached for key $KEYID"
    else
        echo "[gpg-login-preset] Failed to preset passphrase"
    fi
else
    echo "[gpg-login-preset] No GPG passphrase found in keyring"
    echo "[gpg-login-preset] Run: secret-tool store --label='GPG Passphrase' application gpg-passphrase"
fi
