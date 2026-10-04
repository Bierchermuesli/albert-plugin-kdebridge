// Copyright (c) 2026 Stefan Grosser

#include "kwin.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <albert/logging.h>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

static const auto kwin_service = u"org.kde.KWin"_s;
static const auto windows_runner_path = u"/WindowsRunner"_s;

// See WindowsRunnerAction in kwin/src/plugins/krunner-integration
enum WindowsRunnerAction {
    ActivateAction = 0,
    CloseAction = 1,
    MinimizeAction = 2,
    MaximizeAction = 3,
    FullscreenAction = 4,
    KeepAboveAction = 6,
};

// Window scripts. %1 is the window uuid, %2 an optional argument.
static const auto find_window = uR"(
const w = workspace.windowList().find(w => w.internalId.toString() === "%1");
)"_s;

// Moves the window to the current desktop and the active screen, then activates it.
static const auto bring_here_script = find_window + uR"(
if (w) {
    w.minimized = false;
    if (!w.onAllDesktops)
        w.desktops = [workspace.currentDesktop];
    workspace.sendClientToScreen(w, workspace.activeScreen);
    workspace.activeWindow = w;
}
)"_s;

// Moves the window to the desktop with id %2.
static const auto send_to_desktop_script = find_window + uR"(
const d = workspace.desktops.find(d => d.id === "%2");
if (w && d)
    w.desktops = [d];
)"_s;

// Moves the window whose caption starts with %1 (a JS string literal) to the desktop with id %2
// and the active screen, switches to that desktop and activates the window.
static const auto bring_here_by_caption_script = uR"(
const caption = %1;
const w = workspace.windowList().find(w => w.normalWindow && w.caption.startsWith(caption));
const d = workspace.desktops.find(d => d.id === "%2") || workspace.currentDesktop;
if (w) {
    w.minimized = false;
    if (!w.onAllDesktops)
        w.desktops = [d];
    workspace.currentDesktop = d;
    workspace.sendClientToScreen(w, workspace.activeScreen);
    workspace.activeWindow = w;
}
)"_s;

// Toggles whether the window is shown on all desktops.
static const auto toggle_pin_script = find_window + uR"(
if (w)
    w.onAllDesktops = !w.onAllDesktops;
)"_s;

static QDBusMessage callScripting(const QString &path, const QString &interface,
                                  const QString &method, const QVariantList &args = {})
{
    auto msg = QDBusMessage::createMethodCall(kwin_service, path, interface, method);
    msg.setArguments(args);
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, 1000);
}

static void unloadScript(const QString &name)
{
    callScripting(u"/Scripting"_s, u"org.kde.kwin.Scripting"_s, u"unloadScript"_s, {name});
}

static void runWindowsRunnerMatch(const QString &match_id)
{
    auto msg = QDBusMessage::createMethodCall(kwin_service, windows_runner_path,
                                              u"org.kde.krunner1"_s, u"Run"_s);
    msg << match_id << QString();
    QDBusConnection::sessionBus().send(msg);
}

static bool isValidId(const QString &id)
{
    // Ids are interpolated into scripts
    static const QRegularExpression id_regex(uR"(^\{?[0-9a-zA-Z_-]+\}?$)"_s);
    return id_regex.match(id).hasMatch();
}

static void runScript(const QString &name, const QString &script, const QString &script_dir)
{
    QDir().mkpath(script_dir);
    const auto script_name = u"albert-kdebridge-"_s + name;
    const auto script_path = QDir(script_dir).filePath(name + u".js"_s);
    if (QFile file(script_path); file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(script.toUtf8());
    else
    {
        WARN << "Failed writing KWin script:" << file.errorString();
        return;
    }

    unloadScript(script_name);  // Leftovers

    const auto reply = callScripting(u"/Scripting"_s, u"org.kde.kwin.Scripting"_s,
                                     u"loadScript"_s, {script_path, script_name});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
    {
        WARN << "Failed loading KWin script:" << reply.errorMessage();
        return;
    }

    const auto id = reply.arguments().constFirst().toInt();
    callScripting(u"/Scripting/Script%1"_s.arg(id), u"org.kde.kwin.Script"_s, u"run"_s);

    QTimer::singleShot(1000, [script_name]{ unloadScript(script_name); });
}

static void runWindowScript(const QString &name, const QString &script, const QString &uuid,
                            const QString &arg, const QString &script_dir)
{
    if (!isValidId(uuid) || (!arg.isEmpty() && !isValidId(arg)))
    {
        WARN << "Refusing to run script for invalid ids:" << uuid << arg;
        return;
    }
    runScript(name, arg.isEmpty() ? script.arg(uuid) : script.arg(uuid, arg), script_dir);
}

QString kwin::currentDesktop()
{
    auto msg = QDBusMessage::createMethodCall(kwin_service, u"/VirtualDesktopManager"_s,
                                              u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    msg << u"org.kde.KWin.VirtualDesktopManager"_s << u"current"_s;
    const auto reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 200);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().constFirst().value<QDBusVariant>().variant().toString();
}

void kwin::bringHereByCaption(const QString &caption, const QString &desktop_id,
                              const QString &script_dir)
{
    if (caption.isEmpty() || (!desktop_id.isEmpty() && !isValidId(desktop_id)))
        return;

    // Encode the caption as JS string literal, i.e. strip the brackets of a JSON array.
    auto literal = QString::fromUtf8(QJsonDocument(QJsonArray{caption})
                                         .toJson(QJsonDocument::Compact));
    literal = literal.mid(1, literal.size() - 2);

    runScript(u"bringherebycaption"_s,
              // Replace the caption last, it may contain placeholders
              QString(bring_here_by_caption_script).replace(u"%2"_s, desktop_id)
                  .replace(u"%1"_s, literal),
              script_dir);
}

vector<kwin::Desktop> kwin::desktops()
{
    auto msg = QDBusMessage::createMethodCall(kwin_service, u"/VirtualDesktopManager"_s,
                                              u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    msg << u"org.kde.KWin.VirtualDesktopManager"_s << u"desktops"_s;
    const auto reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 200);

    vector<Desktop> desktops;
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
    {
        DEBG << "Failed fetching virtual desktops:" << reply.errorMessage();
        return desktops;
    }

    // a(uss): position, id, name
    const auto arg = reply.arguments().constFirst().value<QDBusVariant>().variant()
                         .value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd())
    {
        uint position;
        Desktop d;
        arg.beginStructure();
        arg >> position >> d.id >> d.name;
        arg.endStructure();
        desktops.emplace_back(::move(d));
    }
    arg.endArray();
    return desktops;
}

bool kwin::isWindowsRunner(const QString &service, const QString &object_path)
{ return service == kwin_service && object_path == windows_runner_path; }

const vector<kwin::WindowActionInfo> &kwin::windowActionInfos()
{
    static const vector<WindowActionInfo> infos{
        {BringHere,     u"bring_here"_s,      u"Bring here"_s},
        {TogglePin,     u"toggle_pin"_s,      u"Toggle on all desktops"_s},
        {SendToDesktop, u"send_to_desktop"_s, u"Send to desktop"_s},
        {Close,         u"close"_s,           u"Close"_s},
        {Minimize,      u"minimize"_s,        u"Minimize"_s},
        {Maximize,      u"maximize"_s,        u"Toggle maximized"_s},
        {Fullscreen,    u"fullscreen"_s,      u"Toggle fullscreen"_s},
        {KeepAbove,     u"keep_above"_s,      u"Toggle keep above"_s},
    };
    return infos;
}

vector<Action> kwin::windowActions(const QString &match_id, const QString &script_dir,
                                   const vector<Desktop> &desktops, uint enabled_actions)
{
    // Desktop matches and others do not get window actions
    const auto prefix = u"%1_"_s.arg(ActivateAction);
    if (!match_id.startsWith(prefix))
        return {};

    const auto uuid = match_id.mid(prefix.size());

    auto runnerAction = [&uuid](WindowsRunnerAction action, QString id, QString text) {
        return Action{
            ::move(id), ::move(text),
            [mid = u"%1_%2"_s.arg(action).arg(uuid)]{ runWindowsRunnerMatch(mid); }
        };
    };

    auto scriptAction = [&](QString id, QString text, QString name, const QString &script,
                            QString arg = {}) {
        return Action{
            ::move(id), ::move(text),
            [name, &script, uuid, arg, script_dir]{
                runWindowScript(name, script, uuid, arg, script_dir);
            }
        };
    };

    vector<Action> actions;

    for (const auto &info : windowActionInfos())
    {
        if (!(enabled_actions & info.flag))
            continue;

        switch (info.flag) {
        case BringHere:
            actions.emplace_back(scriptAction(info.key, info.text, info.key, bring_here_script));
            break;
        case TogglePin:
            actions.emplace_back(scriptAction(info.key, info.text, info.key, toggle_pin_script));
            break;
        case SendToDesktop:
            for (const auto &d : desktops)
                actions.emplace_back(scriptAction(info.key + u'.' + d.id,
                                                  u"Send to %1"_s.arg(d.name),
                                                  info.key, send_to_desktop_script, d.id));
            break;
        case Close:
            actions.emplace_back(runnerAction(CloseAction, info.key, info.text));
            break;
        case Minimize:
            actions.emplace_back(runnerAction(MinimizeAction, info.key, info.text));
            break;
        case Maximize:
            actions.emplace_back(runnerAction(MaximizeAction, info.key, info.text));
            break;
        case Fullscreen:
            actions.emplace_back(runnerAction(FullscreenAction, info.key, info.text));
            break;
        case KeepAbove:
            actions.emplace_back(runnerAction(KeepAboveAction, info.key, info.text));
            break;
        default:
            break;
        }
    }

    return actions;
}
