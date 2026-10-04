// Copyright (c) 2026 Stefan Grosser

#include "klipper.h"
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusMessage>
#include <albert/icon.h>
#include <albert/logging.h>
#include <albert/querycontext.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

static const int max_text_length = 200;

QString Klipper::defaultTrigger() const { return u"clip "_s; }

QString Klipper::synopsis(const QString &) const { return u"<filter>"_s; }

vector<RankItem> Klipper::rankItems(QueryContext &ctx)
{
    auto msg = QDBusMessage::createMethodCall(u"org.kde.klipper"_s, u"/klipper"_s,
                                              u"org.kde.klipper.klipper"_s,
                                              u"getClipboardHistoryMenu"_s);
    const auto reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
    {
        WARN << "Failed to fetch the clipboard history:" << reply.errorMessage();
        return {};
    }

    const auto history = reply.arguments().constFirst().toStringList();
    const auto words = ctx.query().split(u' ', Qt::SkipEmptyParts);

    vector<RankItem> results;
    for (qsizetype i = 0; i < history.size(); ++i)
    {
        const auto &text = history.at(i);

        if (!all_of(words.begin(), words.end(),
                    [&](const QString &w){ return text.contains(w, Qt::CaseInsensitive); }))
            continue;

        auto display = text.simplified();
        if (display.size() > max_text_length)
            display = display.left(max_text_length) + u'…';

        const auto lines = text.count(u'\n') + 1;
        const auto subtext = lines > 1 ? u"%1 lines, %2 characters"_s.arg(lines).arg(text.size())
                                       : u"%1 characters"_s.arg(text.size());

        vector<Action> actions;
        if (havePasteSupport())
            actions.push_back({u"paste"_s, u"Paste"_s,
                               [text]{ setClipboardTextAndPaste(text); }});
        actions.push_back({u"copy"_s, u"Copy"_s, [text]{ setClipboardText(text); }});

        auto item = StandardItem::make(
            QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),
                                                         QCryptographicHash::Md5).toHex()),
            display, subtext,
            []{ return Icon::theme(u"klipper"_s); },
            ::move(actions));

        // Keep the order of the history, newest first
        results.emplace_back(::move(item), 1.0 - double(i) / double(history.size()));
    }

    return results;
}
