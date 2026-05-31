#!/bin/bash

# WSL2 Audio Setup Script
# Routes ALSA through PulseAudio (WSLg)

# Usage: bash WSL2_setup_audio.sh
#   or:  chmod +x WSL2_setup_audio.sh && ./WSL2_setup_audio.sh

echo "=== WSL2 Audio Setup ==="

echo "[0/3] Updating package list..."
sudo apt update

# Install required packages
echo "[1/3] Installing essential packages..."
sudo apt install -y build-essential libncurses-dev alsa-utils

echo "[2/3] Installing ALSA PulseAudio plugin..."
if apt-cache show libasound2-plugins &>/dev/null; then
    sudo apt install -y libasound2-plugins
elif apt-cache show libasound2-plugins:amd64 &>/dev/null; then
    sudo apt install -y libasound2-plugins:amd64
else
    echo "      Warning: libasound2-plugins not found. PulseAudio routing may not work."
    echo "      Try: sudo apt install libasound2-plugins or check your distro's package name."
fi

# Write .asoundrc (skip if already exists)
ASOUNDRC="$HOME/.asoundrc"

if [ -f "$ASOUNDRC" ]; then
    echo "[3/3] $ASOUNDRC already exists. Skipping."
    echo "      To overwrite, delete it manually and re-run."
else
    echo "[3/3] Writing $ASOUNDRC..."
    cat > "$ASOUNDRC" << 'EOF'
pcm.!default {
    type pulse
}
ctl.!default {
    type pulse
}
EOF
    echo "      Done."
fi

echo ""
echo "=== Setup complete. Test with: ==="
echo "aplay /usr/share/sounds/alsa/Front_Center.wav"
