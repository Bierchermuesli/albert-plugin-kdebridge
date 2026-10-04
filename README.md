# KDE Bridge

Albert plugin that brings KDE Plasma services to Albert.

It talks directly to the services that implement the KRunner D-Bus interface (`org.kde.krunner1`), e.g. KWin, the Plasma browser integration or Baloo. KRunner itself is not needed. Every runner found in `krunner/dbusplugins` shows up as its own plugin (`kdebridge.<runner>`) and can be enabled, disabled and triggered like any other Albert plugin.

On top of what the runners provide, the bridge adds some actions:

- Windows: bring here (current desktop and screen), toggle on all desktops, send to desktop, close, minimize, toggle maximized, fullscreen and keep above. Each action can be disabled in the plugin settings.
- Browser tabs: bring the browser window here.
- Any result with a URL: copy URL.

## Build

The plugin is built as part of the Albert source tree. Clone it into `plugins/kdebridge` and build Albert as usual. Requires Qt DBus and Widgets.

## Notes

- Global queries use a short timeout, slow runners like the browser history only show up when triggered, e.g. `browserhistory <query>`.
- The window actions use KWin scripting via D-Bus.
