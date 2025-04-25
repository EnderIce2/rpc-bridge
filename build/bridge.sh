#!/bin/sh

# This script is used to run Steam Play with the bridge.
#  Usage: /path/to/bridge.sh %command%
# Original script: https://github.com/0e4ef622/wine-discord-ipc-bridge/blob/master/winediscordipcbridge-steam.sh
# As requested by https://github.com/EnderIce2/rpc-bridge/issues/2

# Exporting BRIDGE_PATH to provide the bridge with its location.
export BRIDGE_PATH="$(dirname "$0")/bridge.exe"

#  The "--steam" option prevents the game from
# hanging as "running" in Steam after it is closed.
#  This is done by creating a dummy service with
# startup type SERVICE_DEMAND_START so this service
# is only started when we use this script.
BRIDGE_CMD="$BRIDGE_PATH --steam"

# Linux
TEMP_PATH="$XDG_RUNTIME_DIR"
# macOS but Steam Play is not supported on macOS https://github.com/ValveSoftware/Proton/issues/1344
TEMP_PATH=${TEMP_PATH:-"$TMPDIR"}

VESSEL_PATH="$BRIDGE_PATH"
IPC_PATHS="$TEMP_PATH /run/user/$UID $TEMP_PATH/app/com.discordapp.Discord $TEMP_PATH/.flatpak/dev.vencord.Vesktop/xdg-run $TEMP_PATH/snap.discord $TEMP_PATH/snap.discord-canary"
for discord_ipc in $IPC_PATHS; do
	if [ -S "$discord_ipc"/discord-ipc-? ]; then
		VESSEL_PATH="$BRIDGE_PATH:$(echo "$discord_ipc"/discord-ipc-?)"
		break
	fi
done

PROTON_REMOTE_DEBUG_CMD="$BRIDGE_CMD" PRESSURE_VESSEL_FILESYSTEMS_RW="$VESSEL_PATH:$PRESSURE_VESSEL_FILESYSTEMS_RW" "$@"
