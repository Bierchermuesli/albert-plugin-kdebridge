#include "klipper.h"
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QInputDialog>
#include <QProcess>
#include <QStandardPaths>
#include <albert/icon.h>
#include <albert/logging.h>
#include <albert/querycontext.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

static const int max_text_length = 200;

// Command that pastes into the focused window, empty if there is no usable tool. Albert's own
// paste uses xdotool, which reaches X11 windows only. KWin lacks the virtual keyboard protocol
// wtype needs, so on Plasma Wayland it takes ydotool. Shift+Insert rather than Ctrl+V, which
// terminals do not treat as paste; Albert sets clipboard and selection, so either one works.
static QString pasteCommand()
{
    if (qgetenv("XDG_SESSION_TYPE") == "wayland")
    {
        if (!QStandardPaths::findExecutable(u"ydotool"_s).isEmpty())
            // Release modifiers first that may still be held from picking the action
            return u"ydotool key 56:0 100:0 29:0 97:0 125:0 126:0 54:0 42:1 110:1 110:0 42:0"_s;
        return {};
    }
    if (!QStandardPaths::findExecutable(u"xdotool"_s).isEmpty())
        return u"xdotool key --clearmodifiers shift+Insert"_s;
    return {};
}

static void paste(const QString &text)
{
    setClipboardText(text);
    // The focus returns to the previous window only some time after the action ran
    QProcess::startDetached(u"sh"_s, {u"-c"_s, u"sleep 0.5 && "_s + pasteCommand()});
}

static optional<QString> edit(const QString &text)
{
    bool ok = false;
    auto edited = QInputDialog::getMultiLineText(nullptr, u"Edit clipboard item"_s, {}, text, &ok);
    return ok ? optional(edited) : nullopt;
}

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
    const bool can_paste = !pasteCommand().isEmpty();

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
        if (can_paste)
            actions.push_back({u"paste"_s, u"Paste"_s, [text]{ paste(text); }});
        actions.push_back({u"copy"_s, u"Copy"_s, [text]{ setClipboardText(text); }});
        if (can_paste)
            actions.push_back({u"editpaste"_s, u"Edit and paste"_s,
                               [text]{ if (auto e = edit(text)) paste(*e); }});
        actions.push_back({u"edit"_s, u"Edit and copy"_s,
                           [text]{ if (auto e = edit(text)) setClipboardText(*e); }});

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
