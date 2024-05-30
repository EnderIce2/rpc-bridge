# Usage

## GUI

- When running the program manually without providing any arguments it will show a GUI.
![gui](assets/gui.png "rpc-bridge GUI")
- `Start` will start the service without installing itself.
- `Install` will install the service.
- `Remove` will uninstall the service.

## Commands

- `--help` Show help message
    - This will show the help message

- `--version` Show version
    - This will show the version of the program

- `--install` Install the service
    - This will copy the binary to `C:\windows\bridge.exe` and register it as a service

- `--uninstall` Uninstall the service
    - This will remove the service and delete `C:\windows\bridge.exe`

- `--steam` Reserved for Steam
    - This will start the service and exit (used with `bridge.sh`)

- `--no-service` Do not run as service
    - (only for `--steam`)

- `--service` Reserved for service
    - Reserved

- `--rpc <dir>` Set RPC_PATH environment variable
    - This is used to specify the directory where `discord-ipc-0` is located