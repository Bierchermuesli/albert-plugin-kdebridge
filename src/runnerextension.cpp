#include "kwin.h"
#include "runnerextension.h"
#include <QCheckBox>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QVBoxLayout>
#include <albert/logging.h>
#include <atomic>
#include <chrono>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;
using namespace std::chrono_literals;

RunnerExtension::RunnerExtension(RunnerInfo info, RunnerTraits traits, SettingsFactory settings) :
    info_(::move(info)),
    traits_(::move(traits)),
    settings_(::move(settings))
{}

RunnerExtension::~RunnerExtension() = default;

const RunnerTraits &RunnerExtension::traits() const { return traits_; }

void RunnerExtension::loadSettings() {}

QWidget *RunnerExtension::buildConfigWidget() { return nullptr; }

vector<Action> RunnerExtension::actions(const MatchContext &) const { return {}; }

namespace {

QWidget *textWidget(const QString &html)
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *label = new QLabel(html, w);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    l->addWidget(label);
    l->addStretch();
    return w;
}

// -------------------------------------------------------------------------------------------------

class WindowsExtension : public RunnerExtension
{
public:

    using RunnerExtension::RunnerExtension;

    void loadSettings() override
    {
        const auto s = settings_();
        uint enabled = 0;
        for (const auto &action : kwin::windowActionInfos())
            if (s->value(u"window_action_"_s + action.key, true).toBool())
                enabled |= action.flag;
        enabled_actions_ = enabled;
    }

    QWidget *buildConfigWidget() override
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(new QLabel(u"Window actions"_s, w));

        for (const auto &info : kwin::windowActionInfos())
        {
            auto *cb = new QCheckBox(info.text, w);
            cb->setChecked(enabled_actions_ & info.flag);
            QObject::connect(cb, &QCheckBox::toggled, w,
                             [this, flag = info.flag, key = info.key](bool checked) {
                if (checked)
                    enabled_actions_ |= flag;
                else
                    enabled_actions_ &= ~flag;
                settings_()->setValue(u"window_action_"_s + key, checked);
            });
            l->addWidget(cb);
        }

        l->addStretch();
        return w;
    }

    vector<Action> actions(const MatchContext &ctx) const override
    {
        const uint enabled = enabled_actions_;
        if (!enabled)
            return {};
        return kwin::windowActions(ctx.match.id, ctx.cache_dir,
                                   enabled & kwin::SendToDesktop ? desktops()
                                                                 : vector<kwin::Desktop>{},
                                   enabled);
    }

private:

    // Desktops are needed for every match of a query, fetch them once in a while
    vector<kwin::Desktop> desktops() const
    {
        QMutexLocker locker(&desktops_mutex_);
        if (const auto now = chrono::steady_clock::now(); now - desktops_time_ > 2s)
        {
            desktops_ = kwin::desktops();
            desktops_time_ = now;
        }
        return desktops_;
    }

    atomic<uint> enabled_actions_{kwin::AllWindowActions};
    mutable QMutex desktops_mutex_;
    mutable vector<kwin::Desktop> desktops_;
    mutable chrono::steady_clock::time_point desktops_time_;

};

// -------------------------------------------------------------------------------------------------

class BrowserTabsExtension : public RunnerExtension
{
public:

    using RunnerExtension::RunnerExtension;

    vector<Action> actions(const MatchContext &ctx) const override
    {
        // Time the browser needs to activate the tab and update its window caption
        static const int activation_delay_ms = 400;

        return {{
            u"bringhere"_s, u"Bring browser here"_s,
            [service = ctx.service, path = ctx.object_path, mid = ctx.match.id,
             title = ctx.match.text, dir = ctx.cache_dir]{
                // The browser may switch desktops when activating the tab
                const auto desktop = kwin::currentDesktop();
                runRemoteMatch(service, path, mid, {});
                QTimer::singleShot(activation_delay_ms, [title, desktop, dir]{
                    kwin::bringHereByCaption(title, desktop, dir);
                });
            }
        }};
    }

};

// -------------------------------------------------------------------------------------------------

class BalooExtension : public RunnerExtension
{
public:

    using RunnerExtension::RunnerExtension;

    QWidget *buildConfigWidget() override
    {
        // The file indexer daemon registers this service while indexing is enabled
        const auto *iface = QDBusConnection::sessionBus().interface();
        const bool indexing = iface && iface->isServiceRegistered(u"org.kde.baloo"_s).value();

        auto text = uR"(
<p>Searches files using Baloo, the file indexer of KDE Plasma. This makes no sense if you use
another file indexer or the Albert file plugin, it would only duplicate results.</p>
)"_s;
        text += indexing
            ? u"<p>Baloo file indexing is <b>enabled</b> on this system.</p>"_s
            : u"<p><b>Warning:</b> Baloo file indexing is <b>not running</b> on this system, "
              u"this plugin will not find any files. Enable it in System Settings &rarr; "
              u"File Search.</p>"_s;

        return textWidget(text);
    }

};

// -------------------------------------------------------------------------------------------------

class KeePassXCExtension : public RunnerExtension
{
public:

    using RunnerExtension::RunnerExtension;

    QWidget *buildConfigWidget() override
    {
        // The runner reads the entries via the Secret Service, which KeePassXC has to provide
        QString provider;
        if (const auto *iface = QDBusConnection::sessionBus().interface())
            if (const auto pid = iface->servicePid(u"org.freedesktop.secrets"_s); pid.isValid())
                if (QFile comm(u"/proc/%1/comm"_s.arg(pid.value()));
                    comm.open(QIODevice::ReadOnly))
                    provider = QString::fromUtf8(comm.readAll()).trimmed();

        auto text = uR"(
<p>Copies passwords of KeePassXC entries to the clipboard. The runner reads the entries via the
Secret Service, which has to be provided by KeePassXC:</p>
<ol>
<li>KeePassXC: Settings &rarr; Secret Service Integration &rarr; enable it and select the
exposed groups in the database settings.</li>
<li>System Settings &rarr; KDE Wallet: disable <i>Use KWallet for the Secret Service
interface</i>.</li>
<li>Log out and in again.</li>
</ol>
<p>Note: Applications storing secrets via the Secret Service (e.g. browsers) will then use
KeePassXC instead of KWallet. Existing KWallet entries are not migrated.</p>
)"_s;

        if (provider.isEmpty())
            text += u"<p><b>Warning:</b> No Secret Service is running.</p>"_s;
        else if (provider.startsWith(u"keepassxc"_s))
            text += u"<p>The Secret Service is provided by <b>KeePassXC</b>.</p>"_s;
        else
            text += u"<p><b>Warning:</b> The Secret Service is provided by <b>%1</b>, "
                    u"not KeePassXC. This plugin will not work.</p>"_s
                        .arg(provider.toHtmlEscaped());

        return textWidget(text);
    }

};

}  // namespace

// -------------------------------------------------------------------------------------------------

static RunnerTraits traits(QString label, QString badge, QString trigger,
                           QString default_action = u"Open"_s, bool slow = false,
                           QString note = {})
{
    RunnerTraits t;
    t.source_label = ::move(label);
    t.source_badge = ::move(badge);
    t.trigger = ::move(trigger);
    t.default_action = ::move(default_action);
    t.slow_for_global_query = slow;
    t.metadata_note = ::move(note);
    return t;
}

unique_ptr<RunnerExtension> makeRunnerExtension(const RunnerInfo &info, SettingsFactory settings)
{
    const auto &name = info.plugin_name;

    if (name == u"windows"_s || kwin::isWindowsRunner(info.service, info.object_path))
        return make_unique<WindowsExtension>(
            info, traits(u"Window"_s, u"window"_s, u"win "_s, u"Switch to"_s), ::move(settings));

    if (name == u"browsertabs"_s)
        return make_unique<BrowserTabsExtension>(
            info, traits(u"Tab"_s, u"tab-new"_s, u"tabs "_s, u"Switch to"_s), ::move(settings));

    if (name == u"browserhistory"_s)
        return make_unique<RunnerExtension>(
            info, traits(u"History"_s, u"view-history"_s, u"history "_s, u"Open"_s, true),
            ::move(settings));

    if (name == u"baloosearch"_s)
        return make_unique<BalooExtension>(
            info, traits(u"File"_s, u"system-search"_s, u"baloo "_s, u"Open"_s, false,
                         u"Requires Baloo file indexing."_s),
            ::move(settings));

    if (name == u"activities2"_s)
        return make_unique<RunnerExtension>(
            info, traits(u"Activity"_s, u"activities"_s, u"activities "_s), ::move(settings));

    if (name == u"krunner-keepassxc"_s)
        return make_unique<KeePassXCExtension>(
            info, traits(u"KeePassXC"_s, u"password-copy"_s, u"keepass "_s), ::move(settings));

    return make_unique<RunnerExtension>(info, traits(info.name, info.icon, name + u' '),
                                        ::move(settings));
}
