#include "stepperitem.h"
#include <albert/icon.h>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

StepperItem::StepperItem(QString id, QString text, QString icon, Range range,
                         function<QString(int)> format, function<void(int)> apply,
                         vector<Action> other_actions) :
    id_(::move(id)),
    text_(::move(text)),
    icon_(::move(icon)),
    range_(range),
    format_(::move(format)),
    apply_(::move(apply)),
    other_actions_(::move(other_actions)),
    value_(range.value)
{}

QString StepperItem::id() const { return id_; }

QString StepperItem::text() const { return text_; }

QString StepperItem::subtext() const { return u"Current: %1"_s.arg(format_(value_)); }

unique_ptr<Icon> StepperItem::icon() const { return Icon::theme(icon_); }

vector<Action> StepperItem::actions() const
{
    // The actions are created on activation, capturing this is fine
    auto *self = const_cast<StepperItem *>(this);
    auto step = [self](int delta) {
        return Action{
            u"step.%1"_s.arg(delta),
            u"%1%2"_s.arg(delta > 0 ? u"+"_s : u"−"_s, self->format_(abs(delta))),
            [self, delta]{ self->adjust(delta); },
            false  // keep the window open, Enter again steps again
        };
    };

    vector<Action> actions{step(range_.step), step(-range_.step)};
    if (range_.fine_step > 0)
    {
        actions.push_back(step(range_.fine_step));
        actions.push_back(step(-range_.fine_step));
    }
    for (const auto &a : other_actions_)
        actions.push_back(a);
    return actions;
}

void StepperItem::adjust(int delta)
{
    const auto value = clamp(value_ + delta, range_.min, range_.max);
    if (value == value_)
        return;
    value_ = value;
    apply_(value);
    dataChanged();
}
