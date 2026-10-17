#include "quicksettings_p.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <albert/systemutil.h>
#include <cmath>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;
using namespace std::chrono_literals;

namespace qs {

struct Output
{
    QString name;
    bool enabled;
    bool panel;     ///< Built-in laptop panel
    double scale;
    int width;      ///< Logical width
};

vector<Output> fetchOutputs()
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

TimedCache<vector<Output>> outputs_cache{fetchOutputs, 5s};

vector<Output> outputs() { return outputs_cache.get(); }

void kscreenDoctor(const QStringList &args)
{
    runDetachedProcess(QStringList{u"kscreen-doctor"_s} + args);
    outputs_cache.invalidate();
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

}  // namespace qs
