// Copyright (c) 2026 Stefan Grosser

#include "subpluginloader.h"
#include <QTimer>
#include <albert/plugininstance.h>
#include <exception>
using namespace albert;
using namespace std;

SubPluginLoader::SubPluginLoader(PluginMetadata metadata, QString path, Factory factory) :
    metadata_(::move(metadata)),
    path_(::move(path)),
    factory_(::move(factory))
{}

SubPluginLoader::~SubPluginLoader() = default;

QString SubPluginLoader::path() const { return path_; }

const PluginMetadata &SubPluginLoader::metadata() const { return metadata_; }

void SubPluginLoader::load()
{
    QString error;
    try {
        current_loader = this;
        instance_ = factory_();
        holder_.reset(instance_);
    } catch (const exception &e) {
        unload();
        error = QString::fromStdString(e.what());
    }

    // Loading is expected to be asynchronous
    QTimer::singleShot(0, this, [this, error]{ emit finished(error); });
}

void SubPluginLoader::unload()
{
    instance_ = nullptr;
    holder_.reset();
}

PluginInstance *SubPluginLoader::instance() { return instance_; }
