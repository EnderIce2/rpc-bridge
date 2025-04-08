# Discord RPC Bridge for Wine

![GitHub License](https://img.shields.io/github/license/EnderIce2/rpc-bridge?style=for-the-badge)
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/EnderIce2/rpc-bridge/total?style=for-the-badge)
![GitHub Release](https://img.shields.io/github/v/release/EnderIce2/rpc-bridge?style=for-the-badge)
![GitHub Pre-Release](https://img.shields.io/github/v/release/EnderIce2/rpc-bridge?include_prereleases&style=for-the-badge&label=pre-release)

Simple bridge that allows you to use Discord Rich Presence with Wine games/software on Linux/macOS.

[Download latest release](https://github.com/EnderIce2/rpc-bridge/releases/latest/download/bridge.zip "Recommended"){ .md-button .md-button--primary }
[Download latest build](https://github.com/EnderIce2/rpc-bridge/actions/workflows/build.yml "Builds from the latest commits, here be dragons!"){ .md-button }

Works by running a small program in the background that creates a [named pipe](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipes) `\\.\pipe\discord-ipc-0` inside the prefix and forwards all data to the pipe `/run/user/1000/discord-ipc-0`.

This bridge takes advantage of the Windows service implementation in Wine, eliminating the need to run it manually.

These docs are for the latest stable release.  
For v1.0, see [the original README](https://github.com/EnderIce2/rpc-bridge/blob/v1.0/README.md).

---

## Known Issues

- If you use **Vesktop**
  Some games may not show up in Discord. This is because Vesktop uses arRPC, which it doesn't work with some games [#4](https://github.com/EnderIce2/rpc-bridge/issues/4#issuecomment-2143549407). This is not an issue with the bridge.

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
