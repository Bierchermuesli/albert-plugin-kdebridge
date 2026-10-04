// Copyright (c) 2026 Stefan Grosser

#include "quicksettings.h"
#include <QCheckBox>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>
#include <albert/icon.h>
#include <albert/logging.h>
#include <albert/matcher.h>
#include <albert/querycontext.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
#include <cmath>
#include <optional>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

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
<code>night</code>, <code>dnd</code>, <code>power</code>, <code>wifi</code>, <code>dark</code>.
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
        default: break;
        }

        if (item)
            results.emplace_back(::move(item), match.score());
    }

    return results;
}
