#!/bin/bash

# This script is used to create a LaunchAgent on MacOS, to support the service functionality.
# Usage: ./launchd.sh (install|remove)

SYMLINK=/tmp/rpc-bridge/tmpdir
LOCATION=~/Library/Application\ Support/rpc-bridge
SCRIPT=$LOCATION/rpc-bridge
AGENT=~/Library/LaunchAgents/com.enderice2.rpc-bridge.plist

function install() {
    # Directories
    if [ ! -d "$SYMLINK" ]; then
        mkdir -p "$SYMLINK"
    fi
    if [ ! -d "$LOCATION" ]; then
        mkdir -p "$LOCATION"
    fi

    # Link script
    if [ -f "$SCRIPT" ]; then
        rm -f "$SCRIPT"
    fi
    echo "#!/bin/bash
TARGET_DIR=/tmp/rpc-bridge/tmpdir
if [ ! -d "\$TARGET_DIR" ]; then
    mkdir -p "\$TARGET_DIR"
fi
rm -rf "\$TARGET_DIR"
ln -s "\$TMPDIR" "\$TARGET_DIR"" > "$SCRIPT"
    chmod +x "$SCRIPT"

    # LaunchAgent
    if [ -f "$AGENT" ]; then
        rm -f "$AGENT"
    fi
    echo "<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST File Format//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
    <dict>
        <key>Label</key>
        <string>com.enderice2.rpc-bridge</string>
        <key>ProgramArguments</key>
        <array>
            <string>$SCRIPT</string>
        </array>
        <key>RunAtLoad</key>
        <true />
    </dict>
</plist>" > "$AGENT"
    launchctl load "$AGENT"
    echo "LaunchAgent has been installed."
}

function remove() {
    rm -f "$SYMLINK"
    rm -f "$SCRIPT"
    rmdir "$LOCATION"
    if [ -f "$AGENT" ]; then
        launchctl unload "$AGENT"
    fi
    rm -f "$AGENT"
    echo "LaunchAgent has been removed."
}

# CLI
if [ $# -eq 0 ]; then
    echo "Usage: $0 (install|remove)"
    exit 1
fi

case $1 in
    install)
        install
    ;;
    remove)
        remove
    ;;
    *)
        echo "Invalid argument. Please use 'install' or 'remove'."
        exit 1
    ;;
esac
