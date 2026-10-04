# KDE Bridge

Albert plugin that brings KDE Plasma services to Albert.

It talks directly to the services that implement the KRunner D-Bus interface (`org.kde.krunner1`), e.g. KWin, the Plasma browser integration or Baloo. KRunner itself is not needed. Every runner found in `krunner/dbusplugins` shows up as its own plugin (`kdebridge.<runner>`) and can be enabled, disabled and triggered like any other Albert plugin.

Built-in plugins:

- System Settings: search and open System Settings and Info Center pages by name or keyword, e.g. `hdmi` opens the display configuration.
- Quick Settings: show and change night light, do not disturb, power profile, screen and keyboard brightness, Wi-Fi, touchpad and the color scheme, e.g. `dnd`, `power` or `brightness 40`.

On top of what the runners provide, the bridge adds some actions:

- Windows: bring here (current desktop and screen), toggle on all desktops, send to desktop, close, minimize, toggle maximized, fullscreen and keep above. Each action can be disabled in the plugin settings.
- Browser tabs: bring the browser window here.
- Any result with a URL: copy URL.

## Build

The plugin is built as part of the Albert source tree. Clone it into `plugins/kdebridge` and build Albert as usual. Requires Qt DBus and Widgets.

## Notes

- Global queries use a short timeout. The browser history is too slow for it and therefore excluded from the global query by default, use the trigger instead, e.g. `browserhistory <query>`.
- The window actions use KWin scripting via D-Bus.
- File search requires Baloo file indexing. Do not enable it if you use another file indexer.
