#!/bin/bash

# Define target directory
TARGET_DIR=/tmp/rpc-bridge/tmpdir
P_DIR=$(dirname "$TARGET_DIR")
FILE_PATH=$P_DIR/link

# Function to create the symlink and launch agent
function install_link() {
    
    if [ ! -d "$P_DIR" ]; then
        mkdir -p "$P_DIR"
    fi
    
    cat << EOF > "$FILE_PATH"
#!/bin/bash
TARGET_DIR=/tmp/rpc-bridge/tmpdir
if [ ! -d "\$TARGET_DIR" ]; then
    mkdir -p "\$TARGET_DIR"
fi
rm -rf "\$TARGET_DIR"
ln -s "\$TMPDIR" "\$TARGET_DIR"
EOF
    
    chmod +x "$FILE_PATH"
    
    # Launch agent plist file path
    PLIST_FILE=~/Library/LaunchAgents/com.rpc-bridge.tmp-symlink.plist
    
    # Create the launch agent plist with escaped variable
  cat << EOF > "$PLIST_FILE"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST File Format//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.rpc-bridge.tmp-symlink</string>
  <key>ProgramArguments</key>
  <array>
    <string>$FILE_PATH</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
</dict>
</plist>
EOF
    
    # Load the launch agent
    launchctl load "$PLIST_FILE"
    echo "Symlink created and launch agent installed."
}

# Function to remove the symlink and launch agent (unchanged)
function remove_link() {
    # Remove the symlink
    rm -rf "$TARGET_DIR"
    
    # Launch agent plist file path
    PLIST_FILE=~/Library/LaunchAgents/com.rpc-bridge.tmp-symlink.plist
    
    # Unload the launch agent
    launchctl unload "$PLIST_FILE"
    
    # Remove the files
    rm -f "$PLIST_FILE"
    rm -f "$FILE_PATH"
    echo "Symlink removed and launch agent uninstalled."
}

# Check for user input
if [ $# -eq 0 ]; then
    echo "Usage: $0 (install|remove)"
    exit 1
fi

# Action based on user input
case $1 in
    install)
        install_link
    ;;
    remove)
        remove_link
    ;;
    *)
        echo "Invalid argument. Please use 'install' or 'remove'."
        exit 1
    ;;
esac