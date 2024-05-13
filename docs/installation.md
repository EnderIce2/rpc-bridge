# Installation

Installation will copy itself to `C:\windows\bridge.exe` and create a Windows service.  
Logs are stored in `C:\windows\logs\bridge.log`.

## Installing inside a prefix

### Wine (~/.wine)

- Double click `bridge.exe` and click `Install`.
    - ![gui](assets/gui.png "rpc-bridge GUI")
- To remove, the same process can be followed, but click `Remove` instead.

### Lutris

- Click on a game and select `Run EXE inside Wine prefix`.
    - ![lutris](assets/lutris.png "Lutris")
- The same process can be followed as in Wine.

### Steam

There are two ways to install the bridge on Steam.

#### Using bridge.sh[^1]

This method is recommended because it's easier to manage.

- Right click on the game and select `Properties`.
- Under `Set Launch Options`, add the following:
    - ![bridge.sh](assets/steam_script.png "Set Launch Options to the path of the bridge.sh")  
<sup><sub>Of course, you need to replace `/path/to/bridge.sh` with the actual path to the script.</sub></sup>

!!! info "Note"

    `bridge.sh` must be in the same directory as `bridge.exe`.

#### Using Protontricks

- Open [Protontricks](https://github.com/Matoking/protontricks) and select the game you want to install the bridge to.
- Select `Select the default wineprefix`
- Select `Browse files` and copy contents of `build` to the game's prefix `drive_c`
- Select `Run a Wine cmd shell` and run `C:\> install.bat`
    - If you are not in `C:\`, type `c:` and press enter

![protontricks](assets/steam_protontricks.png "If use have the option for 'Run an arbitrary executable (.exe/.msi/.msu), use it instead!")

!!! warning "If you use Flatpak"

    If you are running Steam, Lutris, etc in a Flatpak, you will need to allow the bridge to access the `/run/user/1000/discord-ipc-0` file.

    You can do this by using [Flatseal](https://flathub.org/apps/details/com.github.tchx84.Flatseal) or the terminal.

    === "Flatseal"
    
        Add `xdg-run/discord-ipc-0` under `Filesystems` category  
        ![flatseal](assets/flatseal_permission.png)

    === "Terminal"

        - Per application
            - `flatpak override --filesystem=xdg-run/discord-ipc-0 <flatpak app name>`
        - Globally
            - `flatpak override --user --filesystem=xdg-run/discord-ipc-0`

## macOS

If using the default Wine prefix (`~/.wine`), you can follow the same steps as in Wine.

Registering as a service [is not supported](https://github.com/EnderIce2/rpc-bridge/issues/1#issuecomment-2103423242 "Bridge can't get $TMPDIR unless is set with --rpc. See note below").

!!! info "Note"

    Since macOS doesn't have `/proc/self/environ` and `$TMPDIR` is neither always set nor the same after reboots, so you will need to [add `--rpc "$TMPDIR"` to the `bridge.exe` arguments](https://github.com/EnderIce2/rpc-bridge/issues/1#issuecomment-2104797235).  
    If you don't encounter any issues, you can ignore this step.

## Run without installing the service

If you prefer not to use the service, you can manually run `bridge.exe` within the Wine prefix.  
This method is compatible with both Wine and Lutris.

In Lutris, you can achieve this by adding the path to `bridge.exe` in the `Executable` field under `Game options`. In `Arguments` field, be sure to include the _Windows_ path to the game's executable.

=== "Without bridge"

    ```
        Executable
            /mnt/games/lutris/league-of-legends/drive_c/Riot Games/League of Legends/LeagueClient.exe
        Arguments
            --locale=en_US --launch-product=league_of_legends --launch-patchline=live
    ```

=== "With bridge"

    ```
        Executable
            /mnt/games/lutris/league-of-legends/drive_c/bridge.exe
        Arguments
            "C:\Riot Games\League of Legends\LeagueClient.exe" --locale=en_US --launch-product=league_of_legends --launch-patchline=live
    ```

In Wine, all you need to do is run `bridge.exe` and select `Start`.

## Compiling from source

- Install the `wine`, `x86_64-w64-mingw32-gcc` and `make` packages.
- Open a terminal in the directory that contains this file and run `make`.
- The compiled executable will be located in `build/bridge.exe`.

[^1]: As requested [here](https://github.com/EnderIce2/rpc-bridge/issues/2).
