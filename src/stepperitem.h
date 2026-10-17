#pragma once
#include <QString>
#include <albert/item.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

///
/// An item for a numeric setting that is changed in steps, e.g. the volume.
///
/// The step actions keep the window open, so Enter can be pressed repeatedly while the subtext
/// follows the value. Exact values are better typed, e.g. `brightness 40`.
///
class StepperItem : public albert::detail::DynamicItem
{
public:

    struct Range
    {
        int value;
        int min;
        int max;
        int step;       ///< Coarse step
        int fine_step;  ///< Fine step, 0 for none
    };

    /// _format_ renders the value for the subtext, _apply_ sets it.
    StepperItem(QString id, QString text, QString icon, Range range,
                std::function<QString(int)> format, std::function<void(int)> apply,
                std::vector<albert::Action> other_actions = {});

    QString id() const override;
    QString text() const override;
    QString subtext() const override;
    std::unique_ptr<albert::Icon> icon() const override;
    std::vector<albert::Action> actions() const override;

private:

    void adjust(int delta);

    const QString id_;
    const QString text_;
    const QString icon_;
    const Range range_;
    const std::function<QString(int)> format_;
    const std::function<void(int)> apply_;
    const std::vector<albert::Action> other_actions_;
    std::atomic<int> value_;

};
