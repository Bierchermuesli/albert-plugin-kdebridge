#pragma once
#include <QHash>
#include <QMutex>
#include <QRegularExpression>
#include <QVariantMap>
#include <memory>
#include <albert/extensionplugin.h>
#include <albert/globalqueryhandler.h>
#include <albert/pluginmetadata.h>
struct Appearance;
class RunnerExtension;

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

/// A match returned by a runner.
struct RemoteMatch
{
    QString id;
    QString text;
    QString icon_name;
    double relevance = 0;
    QVariantMap properties;
};

/// Runs _action_id_ (empty for the default action) of the match _match_id_.
void runRemoteMatch(const QString &service, const QString &object_path,
                    const QString &match_id, const QString &action_id);

///
/// Generic KRunner D-Bus runner.
///
class Runner : public albert::ExtensionPlugin,
               public albert::GlobalQueryHandler
{
public:

    Runner(const RunnerInfo &info, std::shared_ptr<const Appearance> appearance);
    ~Runner() override;

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

    QStringList services() const;
    std::vector<RemoteAction> remoteActions(const QString &service);

    const RunnerInfo info_;
    const std::shared_ptr<const Appearance> appearance_;
    const std::unique_ptr<RunnerExtension> extension_;
    const QRegularExpression match_regex_;
    const QString cache_dir_;
    QMutex actions_mutex_;
    QHash<QString, std::vector<RemoteAction>> actions_cache_;

};
