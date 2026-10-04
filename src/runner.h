// Copyright (c) 2026 Stefan Grosser

#pragma once
#include <QHash>
#include <QMutex>
#include <QRegularExpression>
#include <atomic>
#include <albert/extensionplugin.h>
#include <albert/globalqueryhandler.h>
#include <albert/pluginmetadata.h>

///
/// Runner definition parsed from a KRunner D-Bus plugin desktop file.
///
struct RunnerInfo
{
    static RunnerInfo fromDesktopFile(const QString &path);  // throws

    QString path;
    QString plugin_name;
    QString name;
    QString description;
    QString icon;
    QString service;
    QString object_path;
    QString author;
    QString license;
    QString version;
    QString match_regex;
    int min_letter_count = 0;
    bool request_actions_once = false;
};

/// Returns the metadata of the plugin providing _info_.
albert::PluginMetadata runnerMetadata(const RunnerInfo &info,
                                      const albert::PluginMetadata &provider);

class Runner : public albert::ExtensionPlugin,
               public albert::GlobalQueryHandler
{
public:

    explicit Runner(const RunnerInfo &info);

    QString defaultTrigger() const override;
    QWidget *buildConfigWidget() override;
    std::vector<albert::RankItem> rankItems(albert::QueryContext &) override;

private:

    struct RemoteAction
    {
        QString id;
        QString text;
        QString icon;
    };

    QWidget *buildBalooConfigWidget();
    QWidget *buildKeePassXCConfigWidget();
    QStringList services() const;
    std::vector<RemoteAction> remoteActions(const QString &service);
    static void run(const QString &service, const QString &object_path,
                    const QString &match_id, const QString &action_id);

    const RunnerInfo info_;
    const QRegularExpression match_regex_;
    const QString cache_dir_;
    const bool is_windows_runner_;
    const bool is_browser_tabs_runner_;
    std::atomic<uint> window_actions_;
    QMutex actions_mutex_;
    QHash<QString, std::vector<RemoteAction>> actions_cache_;

};
