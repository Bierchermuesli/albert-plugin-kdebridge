// Copyright (c) 2026 Stefan Grosser

#pragma once
#include <albert/extensionplugin.h>
#include <albert/globalqueryhandler.h>
#include <atomic>
#include <memory>

///
/// Shows and changes Plasma quick settings like night light, do not disturb or the power profile.
///
class QuickSettings : public albert::ExtensionPlugin,
                      public albert::GlobalQueryHandler
{
public:

    enum Setting : uint
    {
        NightLight         = 1 << 0,
        DoNotDisturb       = 1 << 1,
        PowerProfile       = 1 << 2,
        Brightness         = 1 << 3,
        KeyboardBrightness = 1 << 4,
        Wifi               = 1 << 5,
        Touchpad           = 1 << 6,
        ColorScheme        = 1 << 7,
        AudioOutput        = 1 << 8,
        AudioMute          = 1 << 9,
        MicrophoneMute     = 1 << 10,
        DisplayMode        = 1 << 11,
        DisplayScale       = 1 << 12,
        ScreensOff         = 1 << 13,
        AllSettings        = 0x3FFF
    };

    QuickSettings();
    ~QuickSettings() override;

    QString defaultTrigger() const override;
    QWidget *buildConfigWidget() override;
    std::vector<albert::RankItem> rankItems(albert::QueryContext &) override;

    /// Inhibitions held by this plugin. Shared with the actions of the items.
    struct Inhibitions;

private:

    std::shared_ptr<Inhibitions> inhibitions_;
    std::atomic<uint> enabled_settings_;

};
