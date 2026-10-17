#pragma once
#include <QDBusConnection>
#include <QDBusMessage>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QVariant>
#include <albert/item.h>
#include <albert/matcher.h>
#include <albert/rankitem.h>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

/// Shared by the quick settings sources.
namespace qs
{

QVariant getProperty(const QDBusConnection &bus, const QString &service, const QString &path,
                     const QString &interface, const QString &name);

QDBusMessage call(const QDBusConnection &bus, const QString &service, const QString &path,
                  const QString &interface, const QString &method, const QVariantList &args = {});

/// Fire and forget, but log errors. E.g. system bus calls may be denied by polkit.
void callAsync(const QDBusConnection &bus, const QString &service, const QString &path,
               const QString &interface, const QString &method, const QVariantList &args = {});

void setPropertyAsync(const QDBusConnection &bus, const QString &service, const QString &path,
                      const QString &interface, const QString &name, const QVariant &value);

albert::Action openSettings(const QString &kcm);

std::shared_ptr<albert::Item> makeItem(const QString &id, const QString &text,
                                       const QString &subtext, const QString &icon,
                                       std::vector<albert::Action> actions);

// States fetched by running a process are expensive. Keep them for a while, the global query
// asks for them on every keystroke.
template<typename T>
class TimedCache
{
public:

    TimedCache(std::function<T()> fetch, std::chrono::milliseconds ttl) : fetch_(std::move(fetch)), ttl_(ttl) {}

    T get()
    {
        QMutexLocker locker(&mutex_);
        if (const auto now = std::chrono::steady_clock::now(); !value_ || now - time_ > ttl_)
        {
            value_ = fetch_();
            time_ = now;
        }
        return *value_;
    }

    void invalidate()
    {
        QMutexLocker locker(&mutex_);
        value_.reset();
    }

private:

    const std::function<T()> fetch_;
    const std::chrono::milliseconds ttl_;
    QMutex mutex_;
    std::optional<T> value_;
    std::chrono::steady_clock::time_point time_;

};

// quicksettings_audio.cpp
std::vector<albert::RankItem> audioOutputItems(const albert::Matcher &matcher,
                                               const QString &query, double score);
std::shared_ptr<albert::Item> muteItem(bool microphone);
/// _requested_ is a percentage to set, otherwise the item is a stepper.
std::shared_ptr<albert::Item> volumeItem(std::optional<int> requested = {});

// quicksettings_display.cpp
std::shared_ptr<albert::Item> displayModeItem();
std::shared_ptr<albert::Item> displayScaleItem();
std::shared_ptr<albert::Item> screensOffItem();

}
