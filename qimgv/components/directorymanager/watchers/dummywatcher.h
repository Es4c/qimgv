#pragma once

#include "directorywatcher.h"

class DummyWatcher : public DirectoryWatcher
{
public:
    DummyWatcher();
    bool isWatchPathReady(const QString& path) const override;

public Q_SLOTS:
    void observe() override;
    void requestWatchPath(const QString& path) override;
};
