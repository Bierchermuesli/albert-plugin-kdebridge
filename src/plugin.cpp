// Copyright (c) 2026 Stefan Grosser

#include "plugin.h"
#include "runner.h"
#include "subpluginloader.h"
#include "systemsettings.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QLabel>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <albert/logging.h>
ALBERT_LOGGING_CATEGORY("kdebridge")
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

Plugin::Plugin()
{
    addSystemSettings();
    addRunners();
}

void Plugin::addSystemSettings()
{
    const auto &provider = loader().metadata();
    PluginMetadata md;
    md.iid = provider.iid;
    md.id = u"kdebridge.systemsettings"_s;
    md.version = provider.version;
    md.name = u"KDE System Settings"_s;
    md.description = u"Open System Settings and Info Center pages. Provided by %1."_s
                         .arg(provider.name);
    md.license = provider.license;
    md.authors = provider.authors;
    md.maintainers = provider.maintainers;
    md.url = provider.url;
    md.binary_dependencies = {u"systemsettings"_s};
    md.load_type = PluginMetadata::LoadType::User;

    loaders_.emplace_back(make_unique<SubPluginLoader>(::move(md), loader().path(), []{
        return new SystemSettings;
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
                    ::move(md), info.path, [info]{ return new Runner(info); }));
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
    text += u"<ul><li><b>%1</b> (<code>kdebridge.systemsettings</code>)</li></ul>"_s
                .arg(name(u"kdebridge.systemsettings"_s).toHtmlEscaped());

    text += tr("<p><b>KRunner D-Bus runners</b></p>");
    text += u"<ul>"_s;
    for (const auto &[id, info] : runners_)
        text += u"<li><b>%1</b> (<code>%2</code>)<br/>%3 &ndash; %4</li>"_s
                    .arg(name(id).toHtmlEscaped(), id.toHtmlEscaped(),
                         info.service.toHtmlEscaped(), status(info.service));
    text += u"</ul>"_s;

    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *label = new QLabel(text, w);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    l->addWidget(label);
    l->addStretch();
    return w;
}
