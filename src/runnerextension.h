#pragma once
#include "runner.h"
#include <QSettings>
#include <QString>
#include <albert/item.h>
#include <functional>
#include <memory>
#include <vector>
class QWidget;

/// Creates the settings of the plugin the extension belongs to.
using SettingsFactory = std::function<std::unique_ptr<QSettings>()>;

/// What an extension needs to build actions for a match.
struct MatchContext
{
    const RemoteMatch &match;
    QString service;
    QString object_path;
    QString cache_dir;
};

/// Static properties of a runner that the generic code cannot know.
struct RunnerTraits
{
    QString source_label;   ///< Short label of the source, e.g. "Tab"
    QString source_badge;   ///< Badge icon of the source
    QString trigger;        ///< Default trigger
    QString default_action = QStringLiteral("Open");  ///< Text of the Run action
    bool slow_for_global_query = false;  ///< Exceeds the global query timeout
    QString metadata_note;  ///< Appended to the plugin description
};

///
/// Runner specific behavior.
///
/// The generic runner talks the KRunner D-Bus protocol. Everything that is specific to a known
/// runner, e.g. window actions or a setup hint, lives in a subclass of this.
///
class RunnerExtension
{
public:

    RunnerExtension(RunnerInfo info, RunnerTraits traits, SettingsFactory settings);
    virtual ~RunnerExtension();

    const RunnerTraits &traits() const;

    /// Called once by the runner on construction. Not called when only the traits are needed.
    virtual void loadSettings();

    /// The caller takes ownership. Returns `nullptr` if there is nothing to configure.
    virtual QWidget *buildConfigWidget();

    /// Returns additional actions for a match. Called from worker threads.
    virtual std::vector<albert::Action> actions(const MatchContext &) const;

protected:

    const RunnerInfo info_;
    const RunnerTraits traits_;
    const SettingsFactory settings_;

};

/// Returns the extension for _info_, a plain one for unknown runners.
std::unique_ptr<RunnerExtension> makeRunnerExtension(const RunnerInfo &info,
                                                     SettingsFactory settings = {});
