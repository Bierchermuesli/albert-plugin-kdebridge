// Copyright (c) 2026 Stefan Grosser

#include "spectacle.h"
#include <albert/icon.h>
#include <albert/indexitem.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

namespace {

struct Mode
{
    QString id;
    QString name;
    QStringList args;
    QStringList keywords;
    bool recording;
};

void spectacle(QStringList args)
{
    args.prepend(u"spectacle"_s);
    runDetachedProcess(args);
}

}  // namespace

QString Spectacle::defaultTrigger() const { return u"shot "_s; }

void Spectacle::updateIndexItems()
{
    static const QStringList screenshot{u"screenshot"_s, u"capture"_s, u"snip"_s, u"print"_s};
    static const QStringList recording{u"record"_s, u"recording"_s, u"video"_s, u"screencast"_s};

    static const vector<Mode> modes{
        {u"region"_s, u"Screenshot Region"_s, {u"--region"_s}, {u"region"_s, u"area"_s}, false},
        {u"window"_s, u"Screenshot Window"_s, {u"--activewindow"_s}, {u"window"_s}, false},
        {u"monitor"_s, u"Screenshot Screen"_s, {u"--current"_s}, {u"screen"_s, u"monitor"_s}, false},
        {u"desktop"_s, u"Screenshot All Screens"_s, {u"--fullscreen"_s}, {u"desktop"_s, u"all"_s}, false},
        {u"record.region"_s, u"Record Region"_s, {u"--record"_s, u"r"_s}, {u"region"_s, u"area"_s}, true},
        {u"record.screen"_s, u"Record Screen"_s, {u"--record"_s, u"s"_s}, {u"screen"_s}, true},
        {u"record.window"_s, u"Record Window"_s, {u"--record"_s, u"w"_s}, {u"window"_s}, true},
    };

    vector<IndexItem> index_items;
    for (const auto &mode : modes)
    {
        vector<Action> actions{
            {u"open"_s, u"Open in Spectacle"_s, [args = mode.args]{ spectacle(args); }}
        };

        // Background mode skips the editor. Delay a bit so Albert is hidden.
        if (!mode.recording)
        {
            actions.push_back({
                u"copy"_s, u"Copy to clipboard"_s,
                [args = mode.args]{
                    spectacle(QStringList{u"--background"_s, u"--nonotify"_s, u"--copy-image"_s,
                                          u"--delay"_s, u"300"_s} + args);
                }
            });
            actions.push_back({
                u"save"_s, u"Save to file"_s,
                [args = mode.args]{
                    spectacle(QStringList{u"--background"_s, u"--delay"_s, u"300"_s} + args);
                }
            });
        }

        auto item = StandardItem::make(
            mode.id, mode.name,
            mode.recording ? u"Screen recording with Spectacle"_s
                           : u"Screenshot with Spectacle"_s,
            [recording = mode.recording]{
                return Icon::theme(recording ? u"media-record"_s : u"spectacle"_s);
            },
            ::move(actions));

        index_items.emplace_back(item, mode.name);
        for (const auto &keyword : (mode.recording ? recording : screenshot) + mode.keywords)
            index_items.emplace_back(item, keyword);
    }

    setIndexItems(::move(index_items));
}
