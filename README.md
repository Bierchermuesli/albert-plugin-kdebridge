# KDE Bridge

Albert plugin that brings KDE Plasma services to Albert: windows, browser tabs, system settings, quick settings, clipboard, screenshots and more.

The bridge provides a set of plugins. Each one shows up in the Albert plugin list as `kdebridge.<name>` with a `KDE` prefix and can be enabled, disabled and triggered on its own. The settings page of the bridge lists them all and shows whether the service behind a runner is running.

Most of them talk directly to the services that implement the KRunner D-Bus interface (`org.kde.krunner1`), e.g. KWin, the Plasma browser integration or Baloo. KRunner itself is not needed. Any runner found in `krunner/dbusplugins` is picked up automatically, the ones below get extra features.

Results of the runners show their source in the subtext and as a badge on the icon, e.g. `Tab · github.com`, so a window, a tab and a history entry of the same page are easy to tell apart. Both can be turned off in the bridge settings.

<!-- screenshot: brave.png -->

## Windows

Trigger `win `. Find and switch to open windows and virtual desktops, works on Wayland. The window icons come from KWin.

Actions on a window:

- Bring here: moves the window to the current desktop and screen and activates it
- Toggle on all desktops
- Send to desktop, one action per virtual desktop
- Close, minimize, toggle maximized, toggle fullscreen, toggle keep above

Each action can be disabled in the plugin settings. The actions use KWin scripting via D-Bus.

<!-- screenshot: kitty.png -->

## Browser Tabs

Trigger `tabs `. Open tabs of browsers with the Plasma browser integration, with favicons.

- Switch to the tab
- Bring browser here: activates the tab and moves the browser window to the current desktop and screen
- Copy URL
- Mute and unmute, for tabs playing audio

## Browser History

Trigger `history `. Searches the browser history via the Plasma browser integration. The history is too slow for the global query and therefore not part of it by default, use the trigger.

- Open
- Copy URL

## System Settings

Trigger `settings `. Opens the pages of System Settings and Info Center. Searches names and the keywords KDE provides for each page, in your language and in English, e.g. `hdmi`, `resolution` or `brightness` find the display configuration, `cpu` the Info Center page. Pages that are not available on the current platform are hidden.

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

<!-- screenshot: mute.png -->

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

Trigger `baloo `. Searches files using Baloo. Requires Baloo file indexing, which Plasma may have disabled. Do not enable this plugin if you use another file indexer or the Albert file plugin, it would only duplicate the results.

## Activities

Trigger `activities ` switches between Plasma activities.

## Build

The plugin is built as part of the Albert source tree. Clone it into `plugins/kdebridge` and build Albert as usual. Requires Qt DBus and Widgets.
