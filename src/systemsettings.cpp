// Copyright (c) 2026 Stefan Grosser

#include "systemsettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QPluginLoader>
#include <QSet>
#include <QStandardPaths>
#include <albert/icon.h>
#include <albert/indexitem.h>
#include <albert/logging.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

namespace {

struct Kcm
{
    QString id;
    QString name;
    QString description;
    QString icon;
    QStringList keywords;
    QStringList commandline;
};

// Returns the value of _key_ in the best matching locale, e.g. Name[de_CH], Name[de], Name.
QString localized(const QJsonObject &object, const QString &key)
{
    const auto locale = QLocale().name();  // e.g. de_CH
    for (const auto &k : {u"%1[%2]"_s.arg(key, locale),
                          u"%1[%2]"_s.arg(key, locale.section(u'_', 0, 0)),
                          key})
        if (const auto v = object.value(k).toString(); !v.isEmpty())
            return v;
    return {};
}

QStringList splitKeywords(const QString &keywords)
{
    QStringList list;
    for (const auto &k : keywords.split(u',', Qt::SkipEmptyParts))
        if (const auto t = k.trimmed(); !t.isEmpty())
            list << t;
    return list;
}

vector<Kcm> scanKcms()
{
    // Some modules are only available on certain platforms, e.g. X11 only
    const auto platform = qEnvironmentVariable("XDG_SESSION_TYPE") == u"wayland"_s
                              ? u"wayland"_s : u"xcb"_s;

    static const vector<pair<QString, QString>> locations{
        {u"plasma/kcms/systemsettings"_s, u"systemsettings"_s},
        {u"plasma/kcms/systemsettings_qwidgets"_s, u"systemsettings"_s},
        {u"plasma/kcms/kinfocenter"_s, u"kinfocenter"_s},
    };

    vector<Kcm> kcms;
    QSet<QString> seen;
    for (const auto &library_path : QCoreApplication::libraryPaths())
    {
        for (const auto &[subdir, app] : locations)
        {
            for (const auto &file_info : QDir(QDir(library_path).filePath(subdir))
                                             .entryInfoList({u"*.so"_s}, QDir::Files))
            {
                const auto id = file_info.completeBaseName();
                if (seen.contains(id))
                    continue;

                // Reads the metadata without loading the library
                const auto md = QPluginLoader(file_info.filePath()).metaData()
                                    .value(u"MetaData"_s).toObject();
                const auto kplugin = md.value(u"KPlugin"_s).toObject();

                if (kplugin.isEmpty() || md.value(u"X-KDE-Test-Module"_s).toBool())
                    continue;

                if (const auto platforms = md.value(u"X-KDE-OnlyShowOnQtPlatforms"_s)
                                               .toVariant().toStringList();
                    !platforms.isEmpty() && !platforms.contains(platform))
                    continue;

                if (const auto try_exec = md.value(u"TryExec"_s).toString();
                    !try_exec.isEmpty() && QStandardPaths::findExecutable(try_exec).isEmpty())
                    continue;

                if (QStandardPaths::findExecutable(app).isEmpty())
                    continue;

                Kcm kcm{
                    .id = id,
                    .name = localized(kplugin, u"Name"_s),
                    .description = localized(kplugin, u"Description"_s),
                    .icon = kplugin.value(u"Icon"_s).toString(),
                    .keywords = splitKeywords(localized(md, u"X-KDE-Keywords"_s)),
                    .commandline = {app, id},
                };

                // Keep the english keywords as well
                if (const auto en = splitKeywords(md.value(u"X-KDE-Keywords"_s).toString());
                    en != kcm.keywords)
                    kcm.keywords << en;

                if (kcm.name.isEmpty())
                    continue;

                seen.insert(id);
                kcms.emplace_back(::move(kcm));
            }
        }
    }
    return kcms;
}

}  // namespace

QString SystemSettings::defaultTrigger() const { return u"settings "_s; }

void SystemSettings::updateIndexItems()
{
    const auto kcms = scanKcms();
    vector<IndexItem> index_items;
    for (const auto &kcm : kcms)
    {
        auto item = StandardItem::make(
            kcm.id,
            kcm.name,
            kcm.description.isEmpty() ? u"System Settings"_s : kcm.description,
            [icon=kcm.icon]{ return Icon::theme(icon.isEmpty() ? u"systemsettings"_s : icon); },
            {{u"open"_s, u"Open"_s, [cmd=kcm.commandline]{ runDetachedProcess(cmd); }}});

        index_items.emplace_back(item, kcm.name);
        for (const auto &keyword : kcm.keywords)
            index_items.emplace_back(item, keyword);
    }

    INFO << u"Indexed %1 System Settings modules"_s.arg(kcms.size());
    setIndexItems(::move(index_items));
}
