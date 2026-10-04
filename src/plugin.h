// Copyright (c) 2026 Stefan Grosser

#pragma once
#include <albert/extensionplugin.h>
#include <albert/pluginprovider.h>
#include <memory>
#include <vector>
class RunnerLoader;

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

    std::vector<std::unique_ptr<RunnerLoader>> loaders_;

};
