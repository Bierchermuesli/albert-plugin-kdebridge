// Copyright (c) 2026 Stefan Grosser

#pragma once
#include <QObject>
#include <albert/pluginloader.h>
#include <albert/pluginmetadata.h>
#include <functional>
#include <memory>

///
/// Loader for the plugins provided by the KDE Bridge.
///
class SubPluginLoader : public albert::PluginLoader
{
public:

    /// Returns a new plugin instance. The loader takes ownership.
    using Factory = std::function<albert::PluginInstance*()>;

    SubPluginLoader(albert::PluginMetadata metadata, QString path, Factory factory);
    ~SubPluginLoader() override;

    QString path() const override;
    const albert::PluginMetadata &metadata() const override;
    void load() override;
    void unload() override;
    albert::PluginInstance *instance() override;

private:

    albert::PluginMetadata metadata_;
    const QString path_;
    const Factory factory_;
    // The destructor of PluginInstance is protected, own it as QObject.
    std::unique_ptr<QObject> holder_;
    albert::PluginInstance *instance_ = nullptr;

};
