// Copyright (c) 2026 Stefan Grosser

#pragma once
#include <albert/extensionplugin.h>
#include <albert/rankedqueryhandler.h>

///
/// Searches the clipboard history of Klipper, the Plasma clipboard.
///
/// Triggered only, the clipboard history would clutter the global query.
///
class Klipper : public albert::ExtensionPlugin,
                public albert::RankedQueryHandler
{
public:

    QString defaultTrigger() const override;
    QString synopsis(const QString &query) const override;
    std::vector<albert::RankItem> rankItems(albert::QueryContext &) override;

};
