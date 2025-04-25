# Installation

Installation will copy itself to `C:\windows\bridge.exe` and create a Windows service.  
Logs are stored in `C:\windows\logs\bridge.log`.

## Preparing macOS for Installation

Before proceeding with the installation, you need to set up a **LaunchAgent** due to the way `$TMPDIR` works on macOS.

- Download the latest build from the [releases](https://github.com/EnderIce2/rpc-bridge/releases).
- Open the archive and make the `launchd.sh` script executable by doing: `chmod +x launchd.sh`.
- To **install** the LaunchAgent, run `./launchd.sh install` and to **remove** it simply run `./launchd.sh remove`.

The script will add a LaunchAgent to your user, that will symlink the `$TMPDIR` directory to `/tmp/rpc-bridge/tmpdir`.

## Video Tutorial on how to install the LaunchAgent + bridge inside CrossOver

![type:video](assets/macos-crossover.mp4){: style='width: 66%; height: 20vw;'}

## Wine (~/.wine)

- Double click `bridge.exe` and click `Install`.
    - ![gui](assets/gui.png "rpc-bridge GUI")
- To remove, the same process can be followed, but click `Remove` instead.

## Run without installing the service

If you prefer not to use the service, you can manually run `bridge.exe` within the prefix, and click on `Start` in the GUI.
