// Copyright (c) 2026 Stefan Grosser

#pragma once
#include "runner.h"
#include <albert/extensionplugin.h>
#include <albert/pluginmetadata.h>
#include <albert/pluginprovider.h>
#include <memory>
#include <vector>
class SubPluginLoader;
struct Appearance;

class Plugin : public albert::ExtensionPlugin,
               public albert::PluginProvider
{
    ALBERT_PLUGIN

public:

    Plugin();
    ~Plugin() override;

    std::vector<albert::PluginLoader*> plugins() const override;
    QWidget *buildConfigWidget() override;

private:

    albert::PluginMetadata builtinMetadata(const QString &id, const QString &name,
                                           const QString &description) const;
    void addSystemSettings();
    void addQuickSettings();
    void addKlipper();
    void addSpectacle();
    void addRunners();

    std::shared_ptr<Appearance> appearance_;
    std::vector<std::unique_ptr<SubPluginLoader>> loaders_;
    QStringList builtins_;  // plugin ids
    std::vector<std::pair<QString, RunnerInfo>> runners_;  // plugin id, info

};
