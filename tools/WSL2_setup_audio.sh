#!/bin/bash

# WSL2 Audio Setup Script
# Routes ALSA through PulseAudio (WSLg)

echo "=== WSL2 Audio Setup ==="

# Install required packages
echo "[1/2] Installing libasound2-plugins..."
sudo apt install -y libasound2-plugins

# Write .asoundrc (skip if already exists)
ASOUNDRC="$HOME/.asoundrc"

if [ -f "$ASOUNDRC" ]; then
    echo "[2/2] $ASOUNDRC already exists. Skipping."
    echo "      To overwrite, delete it manually and re-run."
else
    echo "[2/2] Writing $ASOUNDRC..."
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
