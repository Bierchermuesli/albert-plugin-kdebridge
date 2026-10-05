// Copyright (c) 2026 Stefan Grosser

#include "plugin.h"
#include "runner.h"
#include "subpluginloader.h"
#include "appearance.h"
#include "klipper.h"
#include "quicksettings.h"
#include "spectacle.h"
#include "systemsettings.h"
#include <QCheckBox>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QLabel>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <albert/logging.h>
ALBERT_LOGGING_CATEGORY("kdebridge")
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

Plugin::Plugin() : appearance_(make_shared<Appearance>())
{
    const auto s = settings();
    appearance_->source_subtext = s->value(u"source_subtext"_s, true).toBool();
    appearance_->source_badge = s->value(u"source_badge"_s, true).toBool();

    addSystemSettings();
    addQuickSettings();
    addKlipper();
    addSpectacle();
    addRunners();
}

PluginMetadata Plugin::builtinMetadata(const QString &id, const QString &name,
                                       const QString &description) const
{
    const auto &provider = loader().metadata();
    PluginMetadata md;
    md.iid = provider.iid;
    md.id = u"kdebridge."_s + id;
    md.version = provider.version;
    md.name = name;
    md.description = u"%1. Provided by %2."_s.arg(description, provider.name);
    md.license = provider.license;
    md.authors = provider.authors;
    md.maintainers = provider.maintainers;
    md.url = provider.url;
    md.load_type = PluginMetadata::LoadType::User;
    return md;
}

void Plugin::addSystemSettings()
{
    auto md = builtinMetadata(u"systemsettings"_s, u"KDE System Settings"_s,
                              u"Search System Settings pages by name or keyword"_s);
    md.binary_dependencies = {u"systemsettings"_s};
    builtins_ << md.id;
    loaders_.emplace_back(make_unique<SubPluginLoader>(::move(md), loader().path(), []{
        return new SystemSettings;
    }));
}

void Plugin::addQuickSettings()
{
    auto md = builtinMetadata(u"quicksettings"_s, u"KDE Quick Settings"_s,
                              u"Toggle night light, do not disturb, power profile, brightness, "
                              u"Wi-Fi, touchpad, color scheme, audio and displays"_s);
    builtins_ << md.id;
    loaders_.emplace_back(make_unique<SubPluginLoader>(::move(md), loader().path(), []{
        return new QuickSettings;
    }));
}

void Plugin::addKlipper()
{
    auto md = builtinMetadata(u"klipper"_s, u"KDE Clipboard"_s,
                              u"Search and paste from the Plasma clipboard history"_s);
    builtins_ << md.id;
    loaders_.emplace_back(make_unique<SubPluginLoader>(::move(md), loader().path(), []{
        return new Klipper;
    }));
}

void Plugin::addSpectacle()
{
    auto md = builtinMetadata(u"spectacle"_s, u"KDE Screenshot"_s,
                              u"Take screenshots and screen recordings using Spectacle"_s);
    md.binary_dependencies = {u"spectacle"_s};
    builtins_ << md.id;
    loaders_.emplace_back(make_unique<SubPluginLoader>(::move(md), loader().path(), []{
        return new Spectacle;
    }));
}

void Plugin::addRunners()
{
    // Earlier locations take precedence, e.g. user files shadow system files.
    QSet<QString> seen_files;
    QSet<QString> seen_runners;
    for (const auto &dir : QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                                     u"krunner/dbusplugins"_s,
                                                     QStandardPaths::LocateDirectory))
    {
        for (const auto &file_info : QDir(dir).entryInfoList({u"*.desktop"_s}, QDir::Files,
                                                             QDir::Name))
        {
            if (seen_files.contains(file_info.fileName()))
                continue;
            seen_files.insert(file_info.fileName());

            try {
                auto info = RunnerInfo::fromDesktopFile(file_info.filePath());

                // Some runners ship several desktop files, e.g. KWin for Wayland and X11
                if (seen_runners.contains(info.plugin_name))
                {
                    DEBG << u"Skipping duplicate runner '%1': %2"_s
                                .arg(info.plugin_name, file_info.filePath());
                    continue;
                }
                seen_runners.insert(info.plugin_name);

                auto md = runnerMetadata(info, loader().metadata());
                runners_.emplace_back(md.id, info);
                loaders_.emplace_back(make_unique<SubPluginLoader>(
                    ::move(md), info.path, [info, a = appearance_]{ return new Runner(info, a); }));
            }
            catch (const exception &e) {
                WARN << u"%1: %2"_s.arg(file_info.filePath(), QString::fromStdString(e.what()));
            }
        }
    }

    INFO << u"Found %1 KRunner D-Bus runners"_s.arg(runners_.size());
}

Plugin::~Plugin() = default;

vector<PluginLoader*> Plugin::plugins() const
{
    vector<PluginLoader*> plugins;
    for (auto &loader : loaders_)
        plugins.emplace_back(loader.get());
    return plugins;
}

QWidget *Plugin::buildConfigWidget()
{
    auto bus = QDBusConnection::sessionBus();

    QStringList registered;
    if (auto *iface = bus.interface())
        registered = iface->registeredServiceNames().value();

    QStringList activatable;
    auto msg = QDBusMessage::createMethodCall(u"org.freedesktop.DBus"_s, u"/org/freedesktop/DBus"_s,
                                              u"org.freedesktop.DBus"_s, u"ListActivatableNames"_s);
    if (const auto reply = bus.call(msg, QDBus::Block, 500);
        reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        activatable = reply.arguments().constFirst().toStringList();

    auto status = [&](const QString &service) {
        const auto pattern = QRegularExpression::fromWildcard(
            service, Qt::CaseSensitive, QRegularExpression::NonPathWildcardConversion);
        if (!registered.filter(pattern).isEmpty())
            return tr("available");
        if (!activatable.filter(pattern).isEmpty())
            return tr("started on demand");
        return tr("<b>not running</b>");
    };

    auto name = [this](const QString &id) {
        for (const auto &l : loaders_)
            if (l->metadata().id == id)
                return l->metadata().name;
        return id;
    };

    QString text = tr("<p>Provides the following plugins. "
                      "Enable them and configure their triggers in the plugin list.</p>");

    text += tr("<p><b>Built-in</b></p>");
    text += u"<ul>"_s;
    for (const auto &id : builtins_)
        text += u"<li><b>%1</b> (<code>%2</code>)</li>"_s
                    .arg(name(id).toHtmlEscaped(), id.toHtmlEscaped());
    text += u"</ul>"_s;

    text += tr("<p><b>KRunner D-Bus runners</b></p>");
    text += u"<ul>"_s;
    for (const auto &[id, info] : runners_)
        text += u"<li><b>%1</b> (<code>%2</code>)<br/>%3 &ndash; %4</li>"_s
                    .arg(name(id).toHtmlEscaped(), id.toHtmlEscaped(),
                         info.service.toHtmlEscaped(), status(info.service));
    text += u"</ul>"_s;

    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);

    // Appearance of the runner results
    auto addOption = [&](const QString &option, std::atomic<bool> &value, const QString &key) {
        auto *cb = new QCheckBox(option, w);
        cb->setChecked(value);
        connect(cb, &QCheckBox::toggled, w, [this, &value, key](bool checked) {
            value = checked;
            settings()->setValue(key, checked);
        });
        l->addWidget(cb);
    };
    addOption(tr("Show the source in the subtext, e.g. \"Tab · github.com\""),
              appearance_->source_subtext, u"source_subtext"_s);
    addOption(tr("Show a badge of the source on the icon"),
              appearance_->source_badge, u"source_badge"_s);

    auto *label = new QLabel(text, w);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    l->addWidget(label);
    l->addStretch();
    return w;
}
