// Copyright (c) 2026 Stefan Grosser

#pragma once
#include <albert/extensionplugin.h>
#include <albert/indexqueryhandler.h>

///
/// Takes screenshots and screen recordings using Spectacle.
///
class Spectacle : public albert::ExtensionPlugin,
                  public albert::IndexQueryHandler
{
public:

    QString defaultTrigger() const override;
    void updateIndexItems() override;

};
