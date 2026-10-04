// Copyright (c) 2026 Stefan Grosser

#pragma once
#include <QString>
#include <albert/item.h>
#include <vector>

///
/// Extensions for the KWin windows runner.
///
/// The runner itself has no actions. Its match ids encode the action and the window,
/// i.e. "<action>_<window-uuid>". Use this to provide window actions.
///
namespace kwin
{

struct Desktop
{
    QString id;
    QString name;
};

std::vector<Desktop> desktops();

/// Returns the id of the current virtual desktop or an empty string on error.
QString currentDesktop();

///
/// Moves the first normal window whose caption starts with _caption_ to the desktop with id
/// _desktop_id_ and the active screen, switches to that desktop and activates the window.
///
void bringHereByCaption(const QString &caption, const QString &desktop_id,
                        const QString &script_dir);

enum WindowAction : uint
{
    BringHere     = 1 << 0,
    TogglePin     = 1 << 1,
    SendToDesktop = 1 << 2,
    Close         = 1 << 3,
    Minimize      = 1 << 4,
    Maximize      = 1 << 5,
    Fullscreen    = 1 << 6,
    KeepAbove     = 1 << 7,
    AllWindowActions = 0xFF
};

struct WindowActionInfo
{
    WindowAction flag;
    QString key;   ///< Settings key
    QString text;  ///< Display name
};

const std::vector<WindowActionInfo> &windowActionInfos();

bool isWindowsRunner(const QString &service, const QString &object_path);

std::vector<albert::Action> windowActions(const QString &match_id, const QString &script_dir,
                                          const std::vector<Desktop> &desktops,
                                          uint enabled_actions);

}
