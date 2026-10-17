#include "quicksettings_p.h"
#include "stepperitem.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <albert/systemutil.h>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;
using namespace std::chrono_literals;

namespace qs {

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

TimedCache<pair<QString, vector<Sink>>> sinks_cache{fetchSinks, 5s};

Action switchSinkAction(const Sink &sink)
{
    return {u"sink."_s + sink.name, u"Switch to %1"_s.arg(sink.description),
            [name = sink.name]{
                runDetachedProcess({u"pactl"_s, u"set-default-sink"_s, name});
                sinks_cache.invalidate();
            }};
}

/// Returns the audio output item and items to switch to sinks matching _matcher_.
vector<RankItem> audioOutputItems(const Matcher &matcher, const QString &query, double score)
{
    const auto [default_sink, all_sinks] = sinks_cache.get();
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
        if (const auto m = matcher.match(sink.description); m && !query.isEmpty())
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

optional<bool> fetchMuted(bool microphone)
{
    const auto output = QString::fromUtf8(
        pactl({microphone ? u"get-source-mute"_s : u"get-sink-mute"_s,
               microphone ? u"@DEFAULT_SOURCE@"_s : u"@DEFAULT_SINK@"_s})).trimmed();
    if (output.isEmpty())
        return {};
    return output.endsWith(u"yes"_s);  // "Mute: yes"
}

TimedCache<optional<bool>> audio_muted_cache{[]{ return fetchMuted(false); }, 3s};
TimedCache<optional<bool>> microphone_muted_cache{[]{ return fetchMuted(true); }, 3s};

shared_ptr<Item> muteItem(bool microphone)
{
    const auto muted_state = (microphone ? microphone_muted_cache : audio_muted_cache).get();
    if (!muted_state)
        return {};

    const bool muted = *muted_state;
    const auto device = microphone ? u"@DEFAULT_SOURCE@"_s : u"@DEFAULT_SINK@"_s;
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
                    (microphone ? microphone_muted_cache : audio_muted_cache).invalidate();
                }
            },
            openSettings(u"kcm_pulseaudio"_s)
        });
}

// "Volume: front-left: 42598 /  65% / -11.22 dB,   front-right: ..."
optional<int> fetchVolume()
{
    static const QRegularExpression re(uR"((\d+)%)"_s);
    const auto output = QString::fromUtf8(pactl({u"get-sink-volume"_s, u"@DEFAULT_SINK@"_s}));
    if (const auto m = re.match(output); m.hasMatch())
        return m.captured(1).toInt();
    return {};
}

shared_ptr<Item> volumeItem(optional<int> requested)
{
    const auto current = fetchVolume();
    if (!current)
        return {};

    auto apply = [](int percent) {
        runDetachedProcess({u"pactl"_s, u"set-sink-volume"_s, u"@DEFAULT_SINK@"_s,
                            u"%1%"_s.arg(percent)});
    };
    const auto icon = u"audio-volume-high"_s;

    if (requested)
        return makeItem(u"volume.set"_s, u"Set Volume to %1%"_s.arg(*requested),
                        u"Current: %1%"_s.arg(*current), icon,
                        {{u"set"_s, u"Set to %1%"_s.arg(*requested),
                          [apply, percent = *requested]{ apply(percent); }}});

    return make_shared<StepperItem>(
        u"volume"_s, u"Volume"_s, icon, StepperItem::Range{*current, 0, 100, 5, 1},
        [](int v){ return u"%1%"_s.arg(v); }, apply,
        vector<Action>{openSettings(u"kcm_pulseaudio"_s)});
}

}  // namespace qs
