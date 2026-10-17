#include "appearance.h"
#include "runner.h"
#include "runnerextension.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMutexLocker>
#include <QPainter>
#include <QPalette>
#include <QSettings>
#include <albert/desktopentryparser.h>
#include <albert/icon.h>
#include <albert/logging.h>
#include <albert/plugin.h>
#include <albert/querycontext.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
#include <stdexcept>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

static const auto krunner_interface = u"org.kde.krunner1"_s;
static const auto desktop_group = u"Desktop Entry"_s;

// Global queries are expected to be fast. Do not let a slow runner stall the global query.
static const int global_match_timeout_ms = 200;
static const int triggered_match_timeout_ms = 3000;
static const int actions_timeout_ms = 500;

namespace {

class ImageIcon : public Icon
{
public:

    // The frontend caches pixmaps by url. Use a content hash to get a stable, unique url.
    explicit ImageIcon(QImage image) :
        image_(::move(image)),
        url_(u"kdebridge:%1x%2:%3"_s
                 .arg(image_.width()).arg(image_.height())
                 .arg(qHash(QByteArrayView(image_.constBits(), image_.sizeInBytes())), 0, 16))
    {}

    unique_ptr<Icon> clone() const override { return make_unique<ImageIcon>(*this); }

    void paint(QPainter *p, const QRect &rect) override
    {
        const auto size = image_.size().scaled(rect.size(), Qt::KeepAspectRatio);
        QRect target(QPoint(), size);
        target.moveCenter(rect.center());
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        p->drawImage(target, image_);
    }

    bool isNull() override { return image_.isNull(); }

    QString toUrl() const override { return url_; }

private:

    QImage image_;
    QString url_;

};

// Some runners use 'i', some 'u' for the match type, e.g. a(sssida{sv}) vs. a(sssuda{sv}).
vector<RemoteMatch> parseMatches(const QDBusArgument &arg)
{
    vector<RemoteMatch> matches;
    arg.beginArray();
    while (!arg.atEnd())
    {
        RemoteMatch m;
        arg.beginStructure();
        arg >> m.id >> m.text >> m.icon_name;
        if (arg.currentSignature() == u"u"_s)
        {
            uint type;
            arg >> type;
        }
        else
        {
            int type;
            arg >> type;
        }
        arg >> m.relevance >> m.properties;
        arg.endStructure();
        matches.emplace_back(::move(m));
    }
    arg.endArray();
    return matches;
}

// Struct of the 'icon-data' property: (iiibiiay)
QImage parseIconData(const QVariant &v)
{
    if (!v.canConvert<QDBusArgument>())
        return {};

    const auto arg = v.value<QDBusArgument>();
    int width, height, rowstride, bits_per_sample, channels;
    bool has_alpha;
    QByteArray data;
    arg.beginStructure();
    arg >> width >> height >> rowstride >> has_alpha >> bits_per_sample >> channels >> data;
    arg.endStructure();

    if (width <= 0 || height <= 0 || bits_per_sample != 8
        || data.size() < rowstride * height)
        return {};

    const auto format = has_alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    return QImage(reinterpret_cast<const uchar *>(data.constData()),
                  width, height, rowstride, format).copy();  // detach from data
}

unique_ptr<Icon> makeIcon(const QImage &image, const QString &icon_name, const QString &fallback)
{
    if (!image.isNull())
        return make_unique<ImageIcon>(image);
    if (!icon_name.isEmpty())
        return QFileInfo(icon_name).isAbsolute() ? Icon::image(icon_name)
                                                  : Icon::theme(icon_name);
    return Icon::theme(fallback);
}

// Badge in the bottom right corner. Use the palette for the background, the theme draws its
// glyphs in a color that contrasts with it.
unique_ptr<Icon> badgedIcon(unique_ptr<Icon> icon, const QString &badge)
{
    const auto palette = QGuiApplication::palette();
    return Icon::composed(::move(icon),
                          Icon::iconified(Icon::theme(badge), palette.window(), 0.5, 1,
                                          palette.mid()),
                          0.8, 0.55, 0.0, 0.0, 1.0, 1.0);
}

}  // namespace

// -------------------------------------------------------------------------------------------------

RunnerInfo RunnerInfo::fromDesktopFile(const QString &path)
{
    detail::DesktopEntryParser p(path);

    auto get = [&](const QString &key, QString def = {}) {
        try { return p.getString(desktop_group, key); }
        catch (const out_of_range &) { return def; }
    };
    auto getLocale = [&](const QString &key, QString def = {}) {
        try { return p.getLocaleString(desktop_group, key); }
        catch (const out_of_range &) { return def; }
    };

    RunnerInfo info;
    info.path = path;
    info.service = get(u"X-Plasma-DBusRunner-Service"_s);
    info.object_path = get(u"X-Plasma-DBusRunner-Path"_s);
    if (info.service.isEmpty() || info.object_path.isEmpty())
        throw runtime_error("Missing X-Plasma-DBusRunner-Service or X-Plasma-DBusRunner-Path.");

    if (const auto api = get(u"X-Plasma-API"_s); !api.startsWith(u"DBus"_s))
        throw runtime_error("Unsupported X-Plasma-API: " + api.toStdString());

    info.plugin_name = get(u"X-KDE-PluginInfo-Name"_s, QFileInfo(path).completeBaseName());
    info.plugin_name.remove(QRegularExpression(u"^org\\.kde\\."_s));  // e.g. org.kde.activities2
    info.name = getLocale(u"Name"_s, info.plugin_name);
    info.description = getLocale(u"Comment"_s);
    info.icon = get(u"Icon"_s, u"plasma-search"_s);
    info.author = get(u"X-KDE-PluginInfo-Author"_s);
    info.license = get(u"X-KDE-PluginInfo-License"_s);
    info.version = get(u"X-KDE-PluginInfo-Version"_s, u"1.0"_s);
    info.match_regex = get(u"X-Plasma-Runner-Match-Regex"_s);
    info.min_letter_count = get(u"X-Plasma-Runner-Min-Letter-Count"_s, u"0"_s).toInt();
    info.request_actions_once = get(u"X-Plasma-Request-Actions-Once"_s) == u"true"_s;
    return info;
}

PluginMetadata runnerMetadata(const RunnerInfo &info, const PluginMetadata &provider)
{
    PluginMetadata md;
    md.iid = QString::fromLatin1(ALBERT_PLUGIN_IID);
    md.id = u"kdebridge."_s + info.plugin_name;
    md.version = info.version;
    md.name = u"KDE "_s + info.name;
    md.description = info.description.isEmpty()
                         ? u"Provided by %1."_s.arg(provider.name)
                         : u"%1. Provided by %2."_s.arg(info.description, provider.name);
    if (const auto note = makeRunnerExtension(info)->traits().metadata_note; !note.isEmpty())
        md.description += u' ' + note;
    md.license = info.license;
    if (!info.author.isEmpty())
        md.authors << info.author;
    md.maintainers = provider.maintainers;
    md.url = provider.url;
    md.load_type = PluginMetadata::LoadType::User;
    return md;
}

void runRemoteMatch(const QString &service, const QString &object_path,
                    const QString &match_id, const QString &action_id)
{
    auto msg = QDBusMessage::createMethodCall(service, object_path,
                                              krunner_interface, u"Run"_s);
    msg << match_id << action_id;
    QDBusConnection::sessionBus().send(msg);
}

// -------------------------------------------------------------------------------------------------

Runner::Runner(const RunnerInfo &info, shared_ptr<const Appearance> appearance) :
    info_(info),
    appearance_(::move(appearance)),
    extension_(makeRunnerExtension(info, [this]{ return settings(); })),
    match_regex_(info.match_regex),
    cache_dir_(QString::fromStdString(cacheLocation().string()))
{
    // Slow runners would stall the global query. Exclude them from it by default. The key is
    // read by the core, so only set it if the user has not decided yet.
    if (extension_->traits().slow_for_global_query)
        if (auto s = settings(); !s->contains(u"global_handler_enabled"_s))
            s->setValue(u"global_handler_enabled"_s, false);

    extension_->loadSettings();
}

Runner::~Runner() = default;

QString Runner::defaultTrigger() const { return extension_->traits().trigger; }

QWidget *Runner::buildConfigWidget() { return extension_->buildConfigWidget(); }

QStringList Runner::services() const
{
    // Service names may contain wildcards, e.g. org.kde.plasma.browser_integration*
    if (!info_.service.contains(u'*'))
        return {info_.service};

    QStringList services;
    if (auto *iface = QDBusConnection::sessionBus().interface())
    {
        const auto pattern = QRegularExpression::fromWildcard(info_.service,
                                                              Qt::CaseSensitive,
                                                              QRegularExpression::NonPathWildcardConversion);
        for (const auto &name : iface->registeredServiceNames().value())
            if (pattern.match(name).hasMatch())
                services << name;
    }
    return services;
}

vector<Runner::RemoteAction> Runner::remoteActions(const QString &service)
{
    if (info_.request_actions_once)
    {
        QMutexLocker locker(&actions_mutex_);
        if (auto it = actions_cache_.constFind(service); it != actions_cache_.cend())
            return *it;
    }

    auto msg = QDBusMessage::createMethodCall(service, info_.object_path,
                                              krunner_interface, u"Actions"_s);
    const auto reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, actions_timeout_ms);

    vector<RemoteAction> actions;
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
    {
        const auto arg = reply.arguments().constFirst().value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd())
        {
            RemoteAction a;
            arg.beginStructure();
            arg >> a.id >> a.text >> a.icon;
            arg.endStructure();
            actions.emplace_back(::move(a));
        }
        arg.endArray();
    }
    else
        DEBG << u"%1: Actions failed: %2"_s.arg(id(), reply.errorMessage());

    if (info_.request_actions_once)
    {
        QMutexLocker locker(&actions_mutex_);
        actions_cache_.insert(service, actions);
    }

    return actions;
}

vector<RankItem> Runner::rankItems(QueryContext &ctx)
{
    vector<RankItem> results;

    const auto query = ctx.query().trimmed();
    if (query.size() < info_.min_letter_count)
        return results;

    if (!info_.match_regex.isEmpty() && !match_regex_.match(query).hasMatch())
        return results;

    const auto &traits = extension_->traits();
    const auto source_label = appearance_->source_subtext ? traits.source_label : QString();
    const auto source_badge = appearance_->source_badge ? traits.source_badge : QString();

    auto bus = QDBusConnection::sessionBus();
    for (const auto &service : services())
    {
        auto msg = QDBusMessage::createMethodCall(service, info_.object_path,
                                                  krunner_interface, u"Match"_s);
        msg << query;
        const auto reply = bus.call(msg, QDBus::Block,
                                    ctx.trigger().isEmpty() ? global_match_timeout_ms
                                                            : triggered_match_timeout_ms);

        if (!ctx.isValid())
            break;

        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        {
            DEBG << u"%1: Match failed on %2: %3"_s.arg(id(), service, reply.errorMessage());
            continue;
        }

        const auto matches = parseMatches(reply.arguments().constFirst().value<QDBusArgument>());
        if (matches.empty())
            continue;

        const auto remote_actions = remoteActions(service);

        for (const auto &m : matches)
        {
            vector<Action> actions;
            actions.push_back({
                u"run"_s, traits.default_action,
                [service, path = info_.object_path, mid = m.id]{
                    runRemoteMatch(service, path, mid, {});
                }
            });

            for (auto &a : extension_->actions({m, service, info_.object_path, cache_dir_}))
                actions.emplace_back(::move(a));

            if (const auto urls = m.properties.value(u"urls"_s).toStringList(); !urls.isEmpty())
                actions.push_back({
                    u"copyurl"_s, u"Copy URL"_s,
                    [url = urls.constFirst()]{ setClipboardText(QString(url)); }
                });

            // If the match does not specify actions, all actions apply
            const auto action_ids = m.properties.value(u"actions"_s).toStringList();
            for (const auto &a : remote_actions)
                if (!m.properties.contains(u"actions"_s) || action_ids.contains(a.id))
                    actions.push_back({
                        a.id, a.text,
                        [service, path = info_.object_path, mid = m.id, aid = a.id]{
                            runRemoteMatch(service, path, mid, aid);
                        }
                    });

            const auto subtext = m.properties.value(u"subtext"_s).toString();
            QString item_subtext;
            if (source_label.isEmpty())
                item_subtext = subtext.isEmpty() ? info_.name : subtext;
            else
                item_subtext = subtext.isEmpty() ? source_label
                                                 : u"%1 · %2"_s.arg(source_label, subtext);

            auto item = StandardItem::make(
                m.id, m.text, item_subtext,
                [image = parseIconData(m.properties.value(u"icon-data"_s)),
                 icon_name = m.icon_name, fallback = info_.icon, source_badge]{
                    auto icon = makeIcon(image, icon_name, fallback);
                    return source_badge.isEmpty() ? ::move(icon)
                                                  : badgedIcon(::move(icon), source_badge);
                },
                ::move(actions));

            results.emplace_back(::move(item), qBound(0.0, m.relevance, 1.0));
        }
    }

    return results;
}
