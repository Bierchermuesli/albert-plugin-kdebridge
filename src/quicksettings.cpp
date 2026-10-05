// Copyright (c) 2026 Stefan Grosser

#include "quicksettings.h"
#include <QCheckBox>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <albert/icon.h>
#include <albert/logging.h>
#include <albert/matcher.h>
#include <albert/querycontext.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
#include <chrono>
#include <cmath>
#include <optional>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;
using namespace std::chrono_literals;

struct QuickSettings::Inhibitions
{
    QMutex mutex;
    optional<uint> night_light;
    optional<uint> do_not_disturb;
};

namespace {

const auto kwin_service = u"org.kde.KWin"_s;
const auto night_light_path = u"/org/kde/KWin/NightLight"_s;
const auto night_light_iface = u"org.kde.KWin.NightLight"_s;
const auto notifications_service = u"org.freedesktop.Notifications"_s;
const auto notifications_path = u"/org/freedesktop/Notifications"_s;
const auto power_profiles_service = u"net.hadess.PowerProfiles"_s;
const auto power_profiles_path = u"/net/hadess/PowerProfiles"_s;
const auto brightness_service = u"org.kde.ScreenBrightness"_s;
const auto brightness_path = u"/org/kde/ScreenBrightness"_s;
const auto powerdevil_service = u"org.kde.Solid.PowerManagement"_s;
const auto keyboard_brightness_path = u"/org/kde/Solid/PowerManagement/Actions/KeyboardBrightnessControl"_s;
const auto keyboard_brightness_iface = u"org.kde.Solid.PowerManagement.Actions.KeyboardBrightnessControl"_s;
const auto network_manager_service = u"org.freedesktop.NetworkManager"_s;
const auto network_manager_path = u"/org/freedesktop/NetworkManager"_s;
const auto input_device_path = u"/org/kde/KWin/InputDevice"_s;
const auto input_device_iface = u"org.kde.KWin.InputDevice"_s;

struct SettingInfo
{
    QuickSettings::Setting flag;
    QString key;  ///< Settings key
    QString name;
    QStringList keywords;
};

const vector<SettingInfo> &settingInfos()
{
    static const vector<SettingInfo> infos{
        {QuickSettings::NightLight, u"night_light"_s, u"Night Light"_s,
         {u"night"_s, u"redshift"_s, u"blue light"_s, u"color temperature"_s}},
        {QuickSettings::DoNotDisturb, u"do_not_disturb"_s, u"Do Not Disturb"_s,
         {u"dnd"_s, u"notifications"_s, u"silence"_s, u"focus"_s}},
        {QuickSettings::PowerProfile, u"power_profile"_s, u"Power Profile"_s,
         {u"power"_s, u"battery"_s, u"performance"_s, u"power saver"_s, u"balanced"_s, u"energy"_s}},
        {QuickSettings::Brightness, u"brightness"_s, u"Screen Brightness"_s,
         {u"brightness"_s, u"backlight"_s, u"dim"_s, u"display"_s}},
        {QuickSettings::KeyboardBrightness, u"keyboard_brightness"_s, u"Keyboard Backlight"_s,
         {u"keyboard"_s, u"backlight"_s, u"brightness"_s}},
        {QuickSettings::Wifi, u"wifi"_s, u"Wi-Fi"_s,
         {u"wifi"_s, u"wlan"_s, u"wireless"_s, u"network"_s}},
        {QuickSettings::Touchpad, u"touchpad"_s, u"Touchpad"_s,
         {u"touchpad"_s, u"trackpad"_s}},
        {QuickSettings::ColorScheme, u"color_scheme"_s, u"Color Scheme"_s,
         {u"dark"_s, u"light"_s, u"dark mode"_s, u"theme"_s, u"colors"_s}},
        {QuickSettings::AudioOutput, u"audio_output"_s, u"Audio Output"_s,
         {u"audio"_s, u"sound"_s, u"output"_s, u"speaker"_s, u"headphones"_s, u"hdmi"_s}},
        {QuickSettings::AudioMute, u"audio_mute"_s, u"Mute Audio"_s,
         {u"mute"_s, u"audio"_s, u"sound"_s, u"volume"_s}},
        {QuickSettings::MicrophoneMute, u"microphone_mute"_s, u"Mute Microphone"_s,
         {u"mic"_s, u"microphone"_s, u"mute"_s}},
        {QuickSettings::DisplayMode, u"display_mode"_s, u"Display Mode"_s,
         {u"display"_s, u"screen"_s, u"monitor"_s, u"extend"_s, u"mirror"_s, u"projector"_s,
          u"external"_s}},
        {QuickSettings::DisplayScale, u"display_scale"_s, u"Display Scale"_s,
         {u"scale"_s, u"scaling"_s, u"zoom"_s, u"dpi"_s, u"display"_s, u"screen"_s}},
        {QuickSettings::ScreensOff, u"screens_off"_s, u"Turn Off Screens"_s,
         {u"screen off"_s, u"display off"_s, u"dpms"_s, u"blank"_s}},
    };
    return infos;
}

// -------------------------------------------------------------------------------------------------

QVariant getProperty(const QDBusConnection &bus, const QString &service, const QString &path,
                     const QString &interface, const QString &name)
{
    auto msg = QDBusMessage::createMethodCall(service, path, u"org.freedesktop.DBus.Properties"_s,
                                              u"Get"_s);
    msg << interface << name;
    const auto reply = bus.call(msg, QDBus::Block, 300);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

QDBusMessage call(const QDBusConnection &bus, const QString &service, const QString &path,
                  const QString &interface, const QString &method, const QVariantList &args = {})
{
    auto msg = QDBusMessage::createMethodCall(service, path, interface, method);
    msg.setArguments(args);
    return bus.call(msg, QDBus::Block, 1000);
}

// Fire and forget, but log errors. E.g. system bus calls may be denied by polkit.
void callAsync(const QDBusConnection &bus, const QString &service, const QString &path,
               const QString &interface, const QString &method, const QVariantList &args = {})
{
    auto msg = QDBusMessage::createMethodCall(service, path, interface, method);
    msg.setArguments(args);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [watcher, method]{
        if (watcher->isError())
            WARN << u"%1 failed: %2"_s.arg(method, watcher->error().message());
        watcher->deleteLater();
    });
}

void setPropertyAsync(const QDBusConnection &bus, const QString &service, const QString &path,
                      const QString &interface, const QString &name, const QVariant &value)
{
    callAsync(bus, service, path, u"org.freedesktop.DBus.Properties"_s, u"Set"_s,
              {interface, name, QVariant::fromValue(QDBusVariant(value))});
}

// -------------------------------------------------------------------------------------------------

Action openSettings(const QString &kcm)
{
    return {u"settings"_s, u"Open settings"_s,
            [kcm]{ runDetachedProcess({u"systemsettings"_s, kcm}); }};
}

shared_ptr<Item> makeItem(const QString &id, const QString &text, const QString &subtext,
                          const QString &icon, vector<Action> actions)
{
    return StandardItem::make(id, text, subtext,
                              [icon]{ return Icon::theme(icon); }, ::move(actions));
}

// -------------------------------------------------------------------------------------------------

void inhibitNightLight(const shared_ptr<QuickSettings::Inhibitions> &inhibitions)
{
    const auto reply = call(QDBusConnection::sessionBus(), kwin_service, night_light_path,
                            night_light_iface, u"inhibit"_s);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        WARN << "Failed to pause night light:" << reply.errorMessage();
    else
    {
        QMutexLocker locker(&inhibitions->mutex);
        inhibitions->night_light = reply.arguments().constFirst().toUInt();
    }
}

void uninhibitNightLight(const shared_ptr<QuickSettings::Inhibitions> &inhibitions)
{
    optional<uint> cookie;
    {
        QMutexLocker locker(&inhibitions->mutex);
        swap(cookie, inhibitions->night_light);
    }
    if (cookie)
        call(QDBusConnection::sessionBus(), kwin_service, night_light_path,
             night_light_iface, u"uninhibit"_s, {*cookie});
}

shared_ptr<Item> nightLightItem(const shared_ptr<QuickSettings::Inhibitions> &inhibitions)
{
    const auto bus = QDBusConnection::sessionBus();
    auto get = [&](const QString &name) {
        return getProperty(bus, kwin_service, night_light_path, night_light_iface, name);
    };

    if (!get(u"available"_s).toBool())
        return {};

    const auto enabled = get(u"enabled"_s).toBool();
    const auto running = get(u"running"_s).toBool();
    const auto inhibited = get(u"inhibited"_s).toBool();
    bool ours;
    {
        QMutexLocker locker(&inhibitions->mutex);
        ours = inhibitions->night_light.has_value();
    }

    QString state;
    vector<Action> actions;
    if (ours)
    {
        state = u"Paused by Albert"_s;
        actions.push_back({u"resume"_s, u"Resume"_s,
                           [inhibitions]{ uninhibitNightLight(inhibitions); }});
    }
    else if (inhibited)
        state = u"Paused"_s;
    else if (running)
    {
        state = u"On, %1 K"_s.arg(get(u"currentTemperature"_s).toUInt());
        actions.push_back({u"pause"_s, u"Pause"_s,
                           [inhibitions]{ inhibitNightLight(inhibitions); }});
    }
    else if (enabled)
        state = u"Enabled, currently inactive"_s;
    else
        state = u"Off"_s;

    actions.push_back(openSettings(u"kcm_nightlight"_s));
    return makeItem(u"nightlight"_s, u"Night Light"_s, state, u"redshift-status-on"_s,
                    ::move(actions));
}

// -------------------------------------------------------------------------------------------------

void inhibitNotifications(const shared_ptr<QuickSettings::Inhibitions> &inhibitions)
{
    const auto reply = call(QDBusConnection::sessionBus(), notifications_service,
                            notifications_path, notifications_service, u"Inhibit"_s,
                            {u"albert"_s, u"Do not disturb"_s, QVariantMap{}});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        WARN << "Failed to enable do not disturb:" << reply.errorMessage();
    else
    {
        QMutexLocker locker(&inhibitions->mutex);
        inhibitions->do_not_disturb = reply.arguments().constFirst().toUInt();
    }
}

void uninhibitNotifications(const shared_ptr<QuickSettings::Inhibitions> &inhibitions)
{
    optional<uint> cookie;
    {
        QMutexLocker locker(&inhibitions->mutex);
        swap(cookie, inhibitions->do_not_disturb);
    }
    if (cookie)
        call(QDBusConnection::sessionBus(), notifications_service, notifications_path,
             notifications_service, u"UnInhibit"_s, {*cookie});
}

shared_ptr<Item> doNotDisturbItem(const shared_ptr<QuickSettings::Inhibitions> &inhibitions)
{
    const auto inhibited = getProperty(QDBusConnection::sessionBus(), notifications_service,
                                       notifications_path, notifications_service,
                                       u"Inhibited"_s);
    if (!inhibited.isValid())
        return {};

    bool ours;
    {
        QMutexLocker locker(&inhibitions->mutex);
        ours = inhibitions->do_not_disturb.has_value();
    }

    QString state;
    vector<Action> actions;
    if (ours)
    {
        state = u"On"_s;
        actions.push_back({u"off"_s, u"Turn off"_s,
                           [inhibitions]{ uninhibitNotifications(inhibitions); }});
    }
    else if (inhibited.toBool())
        state = u"On, set by another application"_s;
    else
    {
        state = u"Off"_s;
        actions.push_back({u"on"_s, u"Turn on"_s,
                           [inhibitions]{ inhibitNotifications(inhibitions); }});
    }

    actions.push_back(openSettings(u"kcm_notifications"_s));
    return makeItem(u"donotdisturb"_s, u"Do Not Disturb"_s, state, u"notifications-disabled"_s,
                    ::move(actions));
}

// -------------------------------------------------------------------------------------------------

QString prettyProfile(const QString &profile)
{
    if (profile == u"power-saver"_s)
        return u"Power Save"_s;
    if (profile == u"balanced"_s)
        return u"Balanced"_s;
    if (profile == u"performance"_s)
        return u"Performance"_s;
    return profile;
}

shared_ptr<Item> powerProfileItem()
{
    const auto bus = QDBusConnection::systemBus();
    const auto active = getProperty(bus, power_profiles_service, power_profiles_path,
                                    power_profiles_service, u"ActiveProfile"_s).toString();
    if (active.isEmpty())
        return {};

    QStringList profiles;
    const auto arg = getProperty(bus, power_profiles_service, power_profiles_path,
                                 power_profiles_service, u"Profiles"_s).value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd())
    {
        QVariantMap profile;
        arg >> profile;
        profiles << profile.value(u"Profile"_s).toString();
    }
    arg.endArray();

    // The most likely target first
    vector<Action> actions;
    for (const auto &profile : {u"balanced"_s, u"power-saver"_s, u"performance"_s})
        if (profile != active && profiles.contains(profile))
            actions.push_back({
                u"profile."_s + profile, u"Switch to %1"_s.arg(prettyProfile(profile)),
                [profile]{
                    setPropertyAsync(QDBusConnection::systemBus(), power_profiles_service,
                                     power_profiles_path, power_profiles_service,
                                     u"ActiveProfile"_s, profile);
                }
            });

    actions.push_back(openSettings(u"kcm_powerdevilprofilesconfig"_s));
    return makeItem(u"powerprofile"_s, u"Power Profile"_s,
                    u"Current: %1"_s.arg(prettyProfile(active)),
                    u"battery-profile-powersave"_s, ::move(actions));
}

// -------------------------------------------------------------------------------------------------

/// Returns an item for the first display. _requested_ is a percentage to set.
shared_ptr<Item> brightnessItem(optional<int> requested = {})
{
    const auto bus = QDBusConnection::sessionBus();
    const auto displays = getProperty(bus, brightness_service, brightness_path,
                                      brightness_service, u"DisplaysDBusNames"_s).toStringList();
    if (displays.isEmpty())
        return {};

    const auto path = brightness_path + u'/' + displays.constFirst();
    const auto iface = brightness_service + u".Display"_s;
    const auto brightness = getProperty(bus, brightness_service, path, iface,
                                        u"Brightness"_s).toInt();
    const auto max = getProperty(bus, brightness_service, path, iface,
                                 u"MaxBrightness"_s).toInt();
    if (max <= 0)
        return {};

    auto setAction = [&](int percent) {
        return Action{
            u"set.%1"_s.arg(percent), u"Set to %1%"_s.arg(percent),
            [path, iface, value = max * percent / 100]{
                callAsync(QDBusConnection::sessionBus(), brightness_service, path, iface,
                          u"SetBrightness"_s, {value, 0u});
            }
        };
    };

    vector<Action> actions;
    if (requested)
        actions.push_back(setAction(*requested));
    for (int percent : {25, 50, 75, 100})
        if (!requested || percent != *requested)
            actions.push_back(setAction(percent));
    actions.push_back(openSettings(u"kcm_powerdevilprofilesconfig"_s));

    const auto current = u"%1%"_s.arg(lround(100.0 * brightness / max));
    return makeItem(requested ? u"brightness.set"_s : u"brightness"_s,
                    requested ? u"Set Screen Brightness to %1%"_s.arg(*requested)
                              : u"Screen Brightness"_s,
                    u"Current: %1"_s.arg(current), u"brightness-high"_s, ::move(actions));
}

shared_ptr<Item> keyboardBrightnessItem()
{
    const auto bus = QDBusConnection::sessionBus();
    const auto max_reply = call(bus, powerdevil_service, keyboard_brightness_path,
                                keyboard_brightness_iface, u"keyboardBrightnessMax"_s);
    const auto max = max_reply.arguments().value(0).toInt();
    if (max <= 0)
        return {};

    const auto current = call(bus, powerdevil_service, keyboard_brightness_path,
                              keyboard_brightness_iface, u"keyboardBrightness"_s)
                             .arguments().value(0).toInt();

    vector<Action> actions;
    for (int level = 0; level <= max; ++level)
    {
        if (level == current)
            continue;
        actions.push_back({
            u"level.%1"_s.arg(level),
            level == 0 ? u"Turn off"_s : u"Set to %1%"_s.arg(100 * level / max),
            [level]{
                callAsync(QDBusConnection::sessionBus(), powerdevil_service,
                          keyboard_brightness_path, keyboard_brightness_iface,
                          u"setKeyboardBrightness"_s, {level});
            }
        });
    }

    return makeItem(u"keyboardbrightness"_s, u"Keyboard Backlight"_s,
                    current == 0 ? u"Off"_s : u"Current: %1%"_s.arg(100 * current / max),
                    u"input-keyboard-brightness"_s, ::move(actions));
}

// -------------------------------------------------------------------------------------------------

shared_ptr<Item> wifiItem()
{
    const auto bus = QDBusConnection::systemBus();
    const auto hardware = getProperty(bus, network_manager_service, network_manager_path,
                                      network_manager_service, u"WirelessHardwareEnabled"_s);
    if (!hardware.isValid())
        return {};

    const auto enabled = getProperty(bus, network_manager_service, network_manager_path,
                                     network_manager_service, u"WirelessEnabled"_s).toBool();

    QString state;
    vector<Action> actions;
    if (!hardware.toBool())
        state = u"Disabled by hardware switch"_s;
    else
    {
        state = enabled ? u"On"_s : u"Off"_s;
        actions.push_back({
            u"toggle"_s, enabled ? u"Turn off"_s : u"Turn on"_s,
            [enabled]{
                setPropertyAsync(QDBusConnection::systemBus(), network_manager_service,
                                 network_manager_path, network_manager_service,
                                 u"WirelessEnabled"_s, !enabled);
            }
        });
    }

    actions.push_back(openSettings(u"kcm_networkmanagement"_s));
    return makeItem(u"wifi"_s, u"Wi-Fi"_s, state,
                    enabled ? u"network-wireless"_s : u"network-wireless-disconnected"_s,
                    ::move(actions));
}

// -------------------------------------------------------------------------------------------------

shared_ptr<Item> touchpadItem()
{
    const auto bus = QDBusConnection::sessionBus();
    const auto devices = getProperty(bus, kwin_service, input_device_path,
                                     u"org.kde.KWin.InputDeviceManager"_s,
                                     u"devicesSysNames"_s).toStringList();

    for (const auto &device : devices)
    {
        const auto path = input_device_path + u'/' + device;
        if (!getProperty(bus, kwin_service, path, input_device_iface, u"touchpad"_s).toBool())
            continue;

        const auto enabled = getProperty(bus, kwin_service, path, input_device_iface,
                                         u"enabled"_s).toBool();
        return makeItem(
            u"touchpad"_s, u"Touchpad"_s, enabled ? u"On"_s : u"Off"_s,
            enabled ? u"input-touchpad-on"_s : u"input-touchpad-off"_s,
            {
                {
                    u"toggle"_s, enabled ? u"Turn off"_s : u"Turn on"_s,
                    [path, enabled]{
                        setPropertyAsync(QDBusConnection::sessionBus(), kwin_service, path,
                                         input_device_iface, u"enabled"_s, !enabled);
                    }
                },
                openSettings(u"kcm_touchpad"_s)
            });
    }
    return {};
}

// -------------------------------------------------------------------------------------------------

shared_ptr<Item> colorSchemeItem()
{
    QProcess process;
    process.start(u"plasma-apply-colorscheme"_s, {u"--list-schemes"_s});
    if (!process.waitForFinished(1000) || process.exitCode() != 0)
        return {};

    // " * BreezeDark (current color scheme)"
    static const QRegularExpression re(uR"(^\s*\*\s+(\S+)(\s+\(current color scheme\))?)"_s);
    QStringList schemes;
    QString current;
    for (const auto &line : QString::fromUtf8(process.readAllStandardOutput()).split(u'\n'))
        if (const auto m = re.match(line); m.hasMatch())
        {
            schemes << m.captured(1);
            if (m.hasCaptured(2))
                current = m.captured(1);
        }

    if (schemes.isEmpty())
        return {};

    // Toggle between the dark and light variant of the current scheme
    QString target;
    if (current.contains(u"Dark"_s))
        target = QString(current).replace(u"Dark"_s, u"Light"_s);
    else
        target = current.contains(u"Light"_s) ? QString(current).replace(u"Light"_s, u"Dark"_s)
                                              : u"BreezeDark"_s;
    if (!schemes.contains(target))
        target = current.contains(u"Dark"_s) ? u"BreezeLight"_s : u"BreezeDark"_s;

    auto applyAction = [](const QString &scheme) {
        return Action{
            u"apply."_s + scheme, u"Switch to %1"_s.arg(scheme),
            [scheme]{ runDetachedProcess({u"plasma-apply-colorscheme"_s, scheme}); }
        };
    };

    vector<Action> actions;
    if (schemes.contains(target) && target != current)
        actions.push_back(applyAction(target));
    for (const auto &scheme : schemes)
        if (scheme != current && scheme != target)
            actions.push_back(applyAction(scheme));
    actions.push_back(openSettings(u"kcm_colors"_s));

    return makeItem(u"colorscheme"_s, u"Color Scheme"_s,
                    u"Current: %1"_s.arg(current.isEmpty() ? u"unknown"_s : current),
                    u"preferences-desktop-color"_s, ::move(actions));
}

// -------------------------------------------------------------------------------------------------

/// Runs pactl and returns its stdout. Works with PulseAudio and PipeWire.
QByteArray pactl(const QStringList &args)
{
    QProcess process;
    process.start(u"pactl"_s, args);
    if (!process.waitForFinished(1000) || process.exitCode() != 0)
        return {};
    return process.readAllStandardOutput();
}

struct Sink
{
    QString name;
    QString description;
};

/// Returns the default sink and the sinks that can be used, i.e. have an available port.
pair<QString, vector<Sink>> fetchSinks()
{
    const auto default_sink = QString::fromUtf8(pactl({u"get-default-sink"_s})).trimmed();

    vector<Sink> sinks;
    const auto doc = QJsonDocument::fromJson(pactl({u"-f"_s, u"json"_s, u"list"_s, u"sinks"_s}));
    for (const auto &value : doc.array())
    {
        const auto sink = value.toObject();
        const auto name = sink.value(u"name"_s).toString();

        // Ports without a plugged in device are "not available", e.g. headphones
        const auto ports = sink.value(u"ports"_s).toArray();
        const bool available = ports.isEmpty()
            || any_of(ports.begin(), ports.end(), [](const QJsonValue &port) {
                   return port.toObject().value(u"availability"_s).toString()
                          != u"not available"_s;
               });

        if (available || name == default_sink)
            sinks.push_back({name, sink.value(u"description"_s).toString()});
    }

    // Strip the common prefix of the descriptions, e.g. the name of the sound card
    if (sinks.size() > 1)
    {
        auto prefix = sinks.front().description;
        for (const auto &sink : sinks)
            while (!sink.description.startsWith(prefix))
                prefix.chop(1);
        prefix = prefix.left(prefix.lastIndexOf(u' ') + 1);
        if (!prefix.isEmpty())
            for (auto &sink : sinks)
                sink.description = sink.description.mid(prefix.size());
    }

    return {default_sink, sinks};
}

// Sinks are matched on every global query. Cache them to not spawn processes on each keystroke.
QMutex sinks_mutex;
optional<pair<QString, vector<Sink>>> sinks_cache;
chrono::steady_clock::time_point sinks_cache_time;

pair<QString, vector<Sink>> sinks()
{
    QMutexLocker locker(&sinks_mutex);
    if (!sinks_cache || chrono::steady_clock::now() - sinks_cache_time > 5s)
    {
        sinks_cache = fetchSinks();
        sinks_cache_time = chrono::steady_clock::now();
    }
    return *sinks_cache;
}

void invalidateSinks()
{
    QMutexLocker locker(&sinks_mutex);
    sinks_cache.reset();
}

Action switchSinkAction(const Sink &sink)
{
    return {u"sink."_s + sink.name, u"Switch to %1"_s.arg(sink.description),
            [name = sink.name]{
                runDetachedProcess({u"pactl"_s, u"set-default-sink"_s, name});
                invalidateSinks();
            }};
}

/// Returns the audio output item and items to switch to sinks matching _matcher_.
vector<RankItem> audioOutputItems(const Matcher &matcher, double score)
{
    const auto [default_sink, all_sinks] = sinks();
    if (all_sinks.empty())
        return {};

    QString current = default_sink;
    vector<Action> actions;
    vector<RankItem> items;
    for (const auto &sink : all_sinks)
    {
        if (sink.name == default_sink)
        {
            current = sink.description;
            continue;
        }

        actions.push_back(switchSinkAction(sink));

        // E.g. "headphones" offers switching directly
        if (const auto m = matcher.match(sink.description); m && !matcher.string().isEmpty())
            items.emplace_back(makeItem(u"sink."_s + sink.name,
                                        u"Switch Audio Output to %1"_s.arg(sink.description),
                                        u"Current: %1"_s.arg(current), u"audio-card"_s,
                                        {switchSinkAction(sink)}),
                               m.score());
    }
    actions.push_back(openSettings(u"kcm_pulseaudio"_s));

    if (score >= 0)
        items.emplace_back(makeItem(u"audiooutput"_s, u"Audio Output"_s,
                                    u"Current: %1"_s.arg(current), u"audio-card"_s,
                                    ::move(actions)),
                           score);
    return items;
}

shared_ptr<Item> muteItem(bool microphone)
{
    const auto device = microphone ? u"@DEFAULT_SOURCE@"_s : u"@DEFAULT_SINK@"_s;
    const auto output = QString::fromUtf8(
        pactl({microphone ? u"get-source-mute"_s : u"get-sink-mute"_s, device})).trimmed();
    if (output.isEmpty())
        return {};

    const bool muted = output.endsWith(u"yes"_s);  // "Mute: yes"
    const auto name = microphone ? u"Microphone"_s : u"Audio"_s;
    return makeItem(
        microphone ? u"micmute"_s : u"audiomute"_s,
        u"Mute %1"_s.arg(name),
        muted ? u"Muted"_s : u"Not muted"_s,
        microphone ? (muted ? u"microphone-sensitivity-muted"_s : u"audio-input-microphone"_s)
                   : (muted ? u"audio-volume-muted"_s : u"audio-volume-high"_s),
        {
            {
                u"toggle"_s, muted ? u"Unmute"_s : u"Mute"_s,
                [microphone, device]{
                    runDetachedProcess({u"pactl"_s,
                                        microphone ? u"set-source-mute"_s : u"set-sink-mute"_s,
                                        device, u"toggle"_s});
                }
            },
            openSettings(u"kcm_pulseaudio"_s)
        });
}

// -------------------------------------------------------------------------------------------------

struct Output
{
    QString name;
    bool enabled;
    bool panel;     ///< Built-in laptop panel
    double scale;
    int width;      ///< Logical width
};

vector<Output> outputs()
{
    QProcess process;
    process.start(u"kscreen-doctor"_s, {u"--json"_s});
    if (!process.waitForFinished(2000) || process.exitCode() != 0)
        return {};

    vector<Output> outputs;
    const auto doc = QJsonDocument::fromJson(process.readAllStandardOutput());
    for (const auto &value : doc.object().value(u"outputs"_s).toArray())
    {
        const auto o = value.toObject();
        if (!o.value(u"connected"_s).toBool())
            continue;

        const auto current_mode = o.value(u"currentModeId"_s).toString();
        int width = 0;
        for (const auto &mode : o.value(u"modes"_s).toArray())
            if (mode.toObject().value(u"id"_s).toString() == current_mode)
                width = mode.toObject().value(u"size"_s).toObject().value(u"width"_s).toInt();

        const auto scale = o.value(u"scale"_s).toDouble(1.0);
        outputs.push_back({
            .name = o.value(u"name"_s).toString(),
            .enabled = o.value(u"enabled"_s).toBool(),
            .panel = o.value(u"type"_s).toInt() == 7,  // KScreen::Output::Panel
            .scale = scale,
            .width = int(width / (scale > 0 ? scale : 1.0)),
        });
    }
    return outputs;
}

void kscreenDoctor(const QStringList &args)
{
    runDetachedProcess(QStringList{u"kscreen-doctor"_s} + args);
}

shared_ptr<Item> displayModeItem()
{
    const auto all = outputs();
    if (all.empty())
        return {};

    QStringList enabled;
    for (const auto &o : all)
        if (o.enabled)
            enabled << o.name;

    QString state;
    if (all.size() == 1)
        state = u"One screen: %1"_s.arg(all.front().name);
    else
        state = u"%1 screens connected, enabled: %2"_s.arg(all.size()).arg(enabled.join(u", "_s));

    vector<Action> actions{
        // The display switcher of Plasma, also offers mirroring
        {u"switcher"_s, u"Open display switcher"_s, []{
            callAsync(QDBusConnection::sessionBus(), u"org.kde.kscreen.osdService"_s,
                      u"/org/kde/kscreen/osdService"_s, u"org.kde.kscreen.osdService"_s,
                      u"showActionSelector"_s);
        }}
    };

    const auto panel = find_if(all.begin(), all.end(), [](const Output &o){ return o.panel; });
    if (all.size() > 1 && panel != all.end())
    {
        // Extend: panel left, external screens to the right
        QStringList extend{u"output.%1.enable"_s.arg(panel->name),
                           u"output.%1.position.0,0"_s.arg(panel->name)};
        QStringList external_only{u"output.%1.disable"_s.arg(panel->name)};
        QStringList panel_only{u"output.%1.enable"_s.arg(panel->name)};
        int x = panel->width;
        for (const auto &o : all)
        {
            if (o.panel)
                continue;
            extend << u"output.%1.enable"_s.arg(o.name) << u"output.%1.position.%2,0"_s.arg(o.name).arg(x);
            x += o.width;
            external_only.prepend(u"output.%1.enable"_s.arg(o.name));
            panel_only << u"output.%1.disable"_s.arg(o.name);
        }

        actions.push_back({u"extend"_s, u"Extend"_s, [extend]{ kscreenDoctor(extend); }});
        actions.push_back({u"external"_s, u"External screens only"_s,
                           [external_only]{ kscreenDoctor(external_only); }});
        actions.push_back({u"laptop"_s, u"Laptop screen only"_s,
                           [panel_only]{ kscreenDoctor(panel_only); }});
    }
    actions.push_back(openSettings(u"kcm_kscreen"_s));

    return makeItem(u"displaymode"_s, u"Display Mode"_s, state,
                    u"preferences-desktop-display-randr"_s, ::move(actions));
}

shared_ptr<Item> displayScaleItem()
{
    const auto all = outputs();

    QStringList state;
    vector<Action> actions;
    for (const auto &o : all)
    {
        if (!o.enabled)
            continue;

        const auto current = lround(o.scale * 100);
        state << u"%1: %2%"_s.arg(o.name).arg(current);

        for (int percent : {100, 125, 150, 175, 200})
            if (percent != current)
                actions.push_back({
                    u"scale.%1.%2"_s.arg(o.name).arg(percent),
                    all.size() > 1 ? u"%1: Set to %2%"_s.arg(o.name).arg(percent)
                                   : u"Set to %1%"_s.arg(percent),
                    [name = o.name, percent]{
                        kscreenDoctor({u"output.%1.scale.%2"_s.arg(name).arg(percent / 100.0)});
                    }
                });
    }

    if (state.isEmpty())
        return {};

    actions.push_back(openSettings(u"kcm_kscreen"_s));
    return makeItem(u"displayscale"_s, u"Display Scale"_s, state.join(u", "_s),
                    u"zoom-fit-best"_s, ::move(actions));
}

shared_ptr<Item> screensOffItem()
{
    return makeItem(u"screensoff"_s, u"Turn Off Screens"_s,
                    u"Turns the screens off until the next input"_s, u"system-suspend"_s,
                    {{u"off"_s, u"Turn off"_s, []{
                        // Delay so the key release does not wake the screens immediately
                        QTimer::singleShot(500, []{ kscreenDoctor({u"--dpms"_s, u"off"_s}); });
                    }}});
}

}  // namespace

// -------------------------------------------------------------------------------------------------

QuickSettings::QuickSettings() :
    inhibitions_(make_shared<Inhibitions>()),
    enabled_settings_(AllSettings)
{
    const auto s = settings();
    uint enabled = 0;
    for (const auto &info : settingInfos())
        if (s->value(u"setting_"_s + info.key, true).toBool())
            enabled |= info.flag;
    enabled_settings_ = enabled;
}

QuickSettings::~QuickSettings()
{
    // Do not leave the system paused when the plugin gets disabled
    uninhibitNightLight(inhibitions_);
    uninhibitNotifications(inhibitions_);
}

QString QuickSettings::defaultTrigger() const { return u"qs "_s; }

QWidget *QuickSettings::buildConfigWidget()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);

    auto *label = new QLabel(uR"(
<p>Shows the current state of Plasma quick settings and changes them. Enter applies the most
likely change, the alternative actions offer the other options. Search by name or keyword, e.g.
<code>night</code>, <code>dnd</code>, <code>power</code>, <code>wifi</code>, <code>dark</code>,
<code>mic</code>, <code>headphones</code>, <code>extend</code> or <code>scale</code>.
Set the brightness directly with <code>brightness 40</code>.</p>
<p>Night light and do not disturb are paused by Albert and resumed when Albert quits.</p>
)"_s, w);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    l->addWidget(label);

    for (const auto &info : settingInfos())
    {
        auto *cb = new QCheckBox(info.name, w);
        cb->setChecked(enabled_settings_ & info.flag);
        QObject::connect(cb, &QCheckBox::toggled, w,
                         [this, flag = info.flag, key = info.key](bool checked) {
            if (checked)
                enabled_settings_ |= flag;
            else
                enabled_settings_ &= ~flag;
            settings()->setValue(u"setting_"_s + key, checked);
        });
        l->addWidget(cb);
    }

    l->addStretch();
    return w;
}

vector<RankItem> QuickSettings::rankItems(QueryContext &ctx)
{
    vector<RankItem> results;
    const auto enabled = enabled_settings_.load();

    // "brightness 40"
    static const QRegularExpression brightness_re(
        uR"(^(?:screen\s+)?bright(?:ness)?\s+(\d{1,3})\s*%?$)"_s,
        QRegularExpression::CaseInsensitiveOption);
    if (enabled & Brightness)
        if (const auto m = brightness_re.match(ctx.query().trimmed()); m.hasMatch())
            if (const auto percent = m.captured(1).toInt(); percent <= 100)
            {
                if (auto item = brightnessItem(percent))
                    results.emplace_back(item, 1.0);
                return results;
            }

    const Matcher matcher(ctx.query());
    for (const auto &info : settingInfos())
    {
        if (!(enabled & info.flag))
            continue;

        const auto match = matcher.match(QStringList{info.name} + info.keywords);

        // Matching sinks are offered even if the audio output setting itself does not match
        if (info.flag == AudioOutput)
        {
            for (auto &rank_item : audioOutputItems(matcher, match ? match.score() : -1.0))
                results.emplace_back(::move(rank_item));
            continue;
        }

        if (!match)
            continue;

        if (!ctx.isValid())
            break;

        shared_ptr<Item> item;
        switch (info.flag) {
        case NightLight:         item = nightLightItem(inhibitions_); break;
        case DoNotDisturb:       item = doNotDisturbItem(inhibitions_); break;
        case PowerProfile:       item = powerProfileItem(); break;
        case Brightness:         item = brightnessItem(); break;
        case KeyboardBrightness: item = keyboardBrightnessItem(); break;
        case Wifi:               item = wifiItem(); break;
        case Touchpad:           item = touchpadItem(); break;
        case ColorScheme:        item = colorSchemeItem(); break;
        case AudioMute:          item = muteItem(false); break;
        case MicrophoneMute:     item = muteItem(true); break;
        case DisplayMode:        item = displayModeItem(); break;
        case DisplayScale:       item = displayScaleItem(); break;
        case ScreensOff:         item = screensOffItem(); break;
        default: break;
        }

        if (item)
            results.emplace_back(::move(item), match.score());
    }

    return results;
}
