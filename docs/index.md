# Discord RPC Bridge for Wine

![GitHub License](https://img.shields.io/github/license/EnderIce2/rpc-bridge?style=for-the-badge)
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/EnderIce2/rpc-bridge/total?style=for-the-badge)
![GitHub Release](https://img.shields.io/github/v/release/EnderIce2/rpc-bridge?style=for-the-badge)

Simple bridge that allows you to use Discord Rich Presence with Wine games/software on Linux/macOS.

[Download latest release](https://github.com/EnderIce2/rpc-bridge/releases/latest/download/bridge.zip "Recommended"){ .md-button .md-button--primary }
[Download nightly build](https://github.com/EnderIce2/rpc-bridge/releases/tag/nightly "Builds from the latest commits, here be dragons!"){ .md-button }

Works by running a small program in the background that creates a [named pipe](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipes) `\\.\pipe\discord-ipc-0` inside the prefix and forwards all data to the pipe `/run/user/1000/discord-ipc-0`.

This bridge takes advantage of the Windows service implementation in Wine, eliminating the need to run it manually.

These docs are for the latest stable release.  
For v1.0, see [the original README](https://github.com/EnderIce2/rpc-bridge/blob/v1.0/README.md).

---

## Known Issues

- Wine versions older than 11.5 are not supported on non-x86 systems. (including newer MacBooks with Apple Silicon and other similar hardware)

- For Wine 11.5 and later, you must use the [Staging branch](https://wiki.winehq.org/Wine-Staging).

---

## My game is not showing up in Discord

If your game is not showing up in Discord, please check the following:

- The game you are playing has [Rich Presence](https://discord.com/developers/docs/rich-presence/overview) support!
    - Some games may not have this feature. It's up to developers of the game to implement it.  
    This is not an issue related to the bridge.

- You followed the installation steps correctly.

- You are using the latest version of the bridge. Currently is ![GitHub Release](https://img.shields.io/github/v/release/EnderIce2/rpc-bridge?style=flat-square&label=%20).

### I still want to see the game in Discord!

This is outside the scope of this project, but here is how you can do it:

- Go to `User Settings` under `Activity Settings` in `Registered Games` tab.

- Click `Add it!` and select the game. (e.g. `Cyberpunk2077.exe`, `HytaleClient.exe`, etc.)
    - Note for Linux: This is [not supported](https://github.com/flathub/com.discordapp.Discord/issues/11) if you use Discord from Flatpak.

- If the status shows like below, it means you need to rename the game.
    - ![image](assets/game-activity/wrong-name.png){ width="400" }

- Go again in `Registered Games` and click on the game you just added.

- Rename the game to whatever you want. (in my case `Cyberpunk 2077`)

- The result will be like this:
    - ![image](assets/game-activity/correct-name.png){ width="400" }

- That's it! 

[Official Article](https://support.discord.com/hc/en-us/articles/7931156448919-Activity-Status-Recent-Activity#h_01HTJA8QV5ABSA6FY6GEPMA946)

---

## Compiling from source

- Install the `wine`, `gcc-mingw-w64` and `make` packages.
- Open a terminal in the directory that contains this file and run `make`.
- The compiled executable will be located in `build/bridge.exe`.

---

## Examples

[**League Of Legends**](https://www.leagueoflegends.com/en-us/) running under Wine using Lutris  
![image](assets/lutris_lol.png){ width="600" }

[**Among Us**](https://store.steampowered.com/app/945360/Among_Us/) on Steam  
![image](assets/steam_amongus.png){ width="600" }

[**Content Warning**](https://store.steampowered.com/app/2881650/Content_Warning/) on Steam  
![image](assets/contentwarning.png){ width="600" }

[**Hades**](https://store.steampowered.com/app/1145360/Hades/) on Steam  
![image](assets/hades.png){ width="600" }

[**Lethal Company**](https://store.steampowered.com/app/1966720/Lethal_Company/) ([modded](https://thunderstore.io/c/lethal-company/p/mrov/LethalRichPresence/)) on Steam  
![image](assets/lethalcompany.png){ width="600" }

[**vivid/stasis**](https://store.steampowered.com/app/2093940/vividstasis/) on Steam  
![image](assets/vividstasis.png){ width="600" }

## Credits

This project is inspired by [wine-discord-ipc-bridge](https://github.com/0e4ef622/wine-discord-ipc-bridge).

---
