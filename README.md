# KDE Bridge

Albert plugin that brings KDE Plasma services to Albert: windows, browser tabs, system settings, quick settings, clipboard, screenshots and more.

The bridge provides a set of plugins. Each one shows up in the Albert plugin list as `kdebridge.<name>` with a `KDE` prefix and can be enabled, disabled and triggered on its own. The settings page of the bridge lists them all and shows whether the service behind a runner is running.

Most of them talk directly to the services that implement the KRunner D-Bus interface (`org.kde.krunner1`), e.g. KWin, the Plasma browser integration or Baloo. KRunner itself is not needed. Any runner found in `krunner/dbusplugins` is picked up automatically, the ones below get extra features.

Each plugin takes part in the global search or is used with its trigger only, that is Albert's usual per-plugin setting. The browser history, the file search and the system settings are not part of the global search by default, the clipboard is trigger only.

Results of the runners show their source in the subtext and as a badge on the icon, e.g. `Tab · example.com`, so a window, a tab and a history entry of the same page are easy to tell apart. Both can be turned off in the bridge settings.


## Windows

Trigger `win `. Find and switch to open windows and virtual desktops, works on Wayland. The window icons come from KWin. May be used as Global search.

Actions on a window:

- Bring here: moves the window to the current desktop and screen and activates it
- Toggle on all desktops
- Send to desktop, one action per virtual desktop
- Close, minimize, toggle maximized, toggle fullscreen, toggle keep above

Each action can be disabled in the plugin settings. The actions use KWin scripting via D-Bus.

<img width="892" height="543" alt="image" src="https://github.com/user-attachments/assets/93aad0ce-aca9-4ae6-8f7d-c68b6f3a5a47" />


## Browser Tabs

Trigger `tabs `. Open tabs of browsers with the Plasma browser integration, with favicons.  May be used as Global search.

- Switch to the tab
- Bring browser here: activates the tab and moves the browser window to the current desktop and screen
- Copy URL
- Mute and unmute, for tabs playing audio

## Browser History

Trigger `history `. Searches the browser history via the Plasma browser integration. Not part of the global search by default, it is too slow and too noisy for it.

- Open
- Copy URL

## System Settings

Trigger `settings `, not part of the global search by default. Opens the pages of System Settings and Info Center. Searches names and the keywords KDE provides for each page, in your language and in English, e.g. `hdmi`, `resolution` or `brightness` find the display configuration, `cpu` the Info Center page. Pages that are not available on the current platform are hidden.

## Quick Settings

Trigger `qs `. Shows the current state of Plasma quick settings and changes them. Enter applies the most likely change, the other options are available as actions.

| Setting | Search for | Actions |
|---|---|---|
| Night Light | `night` | pause, resume |
| Do Not Disturb | `dnd` | turn on, turn off |
| Power Profile | `power` | switch to balanced, power save, performance |
| Screen Brightness | `brightness`, `brightness 40` | set to 25, 50, 75, 100 % or the given value |
| Keyboard Backlight | `keyboard` | off, 50 %, 100 % |
| Wi-Fi | `wifi` | turn on, turn off |
| Touchpad | `touchpad` | turn on, turn off |
| Color Scheme | `dark`, `light` | switch between the schemes |
| Audio Output | `audio`, `headphones`, `hdmi` | switch to an output that is plugged in |
| Mute Audio, Mute Microphone | `mute`, `mic` | mute, unmute |
| Display Mode | `display`, `extend` | display switcher, extend, external only, laptop only |
| Display Scale | `scale` | 100 to 200 % per screen |
| Turn Off Screens | `screen off` | turn off until the next input |

Every setting has an action to open the matching System Settings page and can be disabled in the plugin settings. Night light and do not disturb are paused by Albert and resumed when Albert quits.


## Clipboard

Trigger `clip `. Searches the clipboard history of Klipper, newest first. All words of the query have to match.

- Paste
- Copy

## Screenshot

Trigger `shot `. Screenshots and screen recordings with Spectacle: region, window, screen, all screens, and recordings of region, screen and window.

- Open in Spectacle
- Copy to clipboard, without the editor
- Save to file, without the editor

## File Search

Trigger `baloo `. Searches files using Baloo, not part of the global search by default. Requires Baloo file indexing, which Plasma may have disabled. May be redundant if you use another file indexer or the Albert file plugin.

## Activities

Trigger `activities ` switches between Plasma activities.

## Try it

Albert loads native plugins from `~/.local/lib/albert/`. The plugin has to be built against the Albert version that is installed. The official Albert packages ship the headers and CMake files needed for that, so the Albert source tree is not required.

Debian/Ubuntu, other distributions need the equivalent Qt 6 development packages:

```
sudo apt install cmake ninja-build qt6-base-dev qt6-tools-dev qt6-l10n-tools
git clone https://github.com/Bierchermuesli/albert-plugin-kdebridge
cd albert-plugin-kdebridge
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
mkdir -p ~/.local/lib/albert && cp build/kdebridge.so ~/.local/lib/albert/
```

Restart Albert, then enable `KDE Bridge` and the plugins you want in the settings. After an Albert update the plugin has to be rebuilt. To get rid of it, delete the file.

For development the plugin can also be cloned into `plugins/kdebridge` of the Albert source tree and built with Albert.

## Requirements

KDE Plasma 6 on Linux, Wayland or X11. Some features need the tools behind them, the matching results are simply missing otherwise:

- Quick Settings: `pactl` for audio, `kscreen-doctor` for displays, `plasma-apply-colorscheme` for the color scheme
- Browser tabs and history: the Plasma browser integration extension in the browser
- File search: Baloo file indexing

## Todo

before going upstream:

- i18n Translations
- Settings pages as `.ui` files instead of HTML strings
