
<div align="right">
  <details>
    <summary >🌐 Language</summary>
    <div>
      <div align="center">
        <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=en">English</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=zh-CN">简体中文</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=zh-TW">繁體中文</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=ja">日本語</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=ko">한국어</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=hi">हिन्दी</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=th">ไทย</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=fr">Français</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=de">Deutsch</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=es">Español</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=it">Italiano</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=ru">Русский</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=pt">Português</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=nl">Nederlands</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=pl">Polski</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=ar">العربية</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=fa">فارسی</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=tr">Türkçe</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=vi">Tiếng Việt</a>
        | <a href="https://openaitx.github.io/view.html?user=EnderIce2&project=rpc-bridge&lang=id">Bahasa Indonesia</a>
      </div>
    </div>
  </details>
</div>

# Discord RPC Bridge for Wine

![GitHub License](https://img.shields.io/github/license/EnderIce2/rpc-bridge?style=for-the-badge)
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/EnderIce2/rpc-bridge/total?style=for-the-badge)
![GitHub Release](https://img.shields.io/github/v/release/EnderIce2/rpc-bridge?style=for-the-badge)

Simple bridge that allows you to use Discord Rich Presence with Wine games/software.

Works by running a small program in the background that creates a [named pipe](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipes) `\\.\pipe\discord-ipc-0` inside the prefix and forwards all data to the pipe `/run/user/1000/discord-ipc-0`.

This bridge takes advantage of the Windows service implementation in Wine, eliminating the need to manually run any programs.

---

## Installation & Usage

Installation will copy itself to `C:\windows\bridge.exe` and create a Windows service.  
Logs are stored in `C:\windows\logs\bridge.log`.

#### Installing inside a prefix

##### Wine (~/.wine)

- Double click `bridge.exe` and click `Install`.
    - ![gui](docs/assets/gui.png)
- To remove, the same process can be followed, but click `Remove` instead.

*Note, an [extra step](https://github.com/EnderIce2/rpc-bridge?tab=readme-ov-file#macos) is needed on MacOS*

##### Lutris

- Click on a game and select `Run EXE inside Wine prefix`.
    - ![lutris](docs/assets/lutris.png)
- The same process can be followed as in Wine.

##### Steam

- Right click on the game and select `Properties`.
- Under `Set Launch Options`, add the following:
    - ![bridge.sh](docs/assets/steam_script.png "Set Launch Options to the path of the bridge.sh")
- The `bridge.sh` script must be in the same directory as `bridge.exe`.

#### If you use Flatpak

- If you are running Steam, Lutris, etc in a Flatpak, you will need to allow the bridge to access the `/run/user/1000/discord-ipc-0` file.
	- ##### By using [Flatseal](https://flathub.org/apps/details/com.github.tchx84.Flatseal)
		- Add `xdg-run/discord-ipc-0` under `Filesystems` category
			- ![flatseal](docs/assets/flatseal_permission.png)
	- ##### By using the terminal
		- Per application
			- `flatpak override --filesystem=xdg-run/discord-ipc-0 <flatpak app name>`
		- Globally
			- `flatpak override --user --filesystem=xdg-run/discord-ipc-0`

##### MacOS

The steps for MacOS are almost the same, but due to the way `$TMPDIR` works, you will have to install a **LaunchAgent**.

- Download the latest build from the [releases](https://github.com/EnderIce2/rpc-bridge/releases)
- Open the archive and make the `launchd.sh` script executable by doing: `chmod +x launchd.sh`
- To **install** the LaunchAgent, run `./launchd.sh install` and to **remove** it simply run `./launchd.sh remove`.

The script will add a LaunchAgent to your user, that will symlink the `$TMPDIR` directory to `/tmp/rpc-bridge/tmpdir`.

*Note: You will need to launch the `bridge.exe` file manually in Wine at least once for it to register and launch automatically the next time.*

More details on how to install the LaunchAgent can be found in the [documentation](https://enderice2.github.io/rpc-bridge/).

## Compiling from source

- Install the `wine`, `gcc-mingw-w64` and `make` packages.
- Open a terminal in the directory that contains this file and run `make`.
- The compiled executable will be located in `build/bridge.exe`.

## Credits

This project is inspired by [wine-discord-ipc-bridge](https://github.com/0e4ef622/wine-discord-ipc-bridge).

---
