// Copyright (c) 2026 Stefan Grosser

#pragma once
#include <albert/extensionplugin.h>
#include <albert/indexqueryhandler.h>

///
/// Opens System Settings and Info Center pages (KCMs).
///
class SystemSettings : public albert::ExtensionPlugin,
                       public albert::IndexQueryHandler
{
public:

    QString defaultTrigger() const override;
    QWidget *buildConfigWidget() override;
    void updateIndexItems() override;

};
