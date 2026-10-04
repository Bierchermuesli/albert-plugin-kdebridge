// Copyright (c) 2026 Stefan Grosser

#pragma once
#include "runner.h"
#include <albert/extensionplugin.h>
#include <albert/pluginprovider.h>
#include <memory>
#include <vector>
class SubPluginLoader;

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

    void addSystemSettings();
    void addRunners();

    std::vector<std::unique_ptr<SubPluginLoader>> loaders_;
    std::vector<std::pair<QString, RunnerInfo>> runners_;  // plugin id, info

};
