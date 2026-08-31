// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#include "libsync/vfs/vfs.h"

#include <QCoreApplication>
#include <QTimer>

#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTimer::singleShot(0, &app, [] {
        for (int i = 0; i <= static_cast<int>(OCC::Vfs::Mode::OpenVFS); ++i) {
            const auto mode = static_cast<OCC::Vfs::Mode>(i);
            auto instance = OCC::VfsPluginManager::instance().createVfsFromPlugin(mode);
            std::cout << "Plugin: " << OCC::Utility::enumToString(mode).toStdString() << (instance ? " is available" : " is unavailable") << std::endl;
        }
        QCoreApplication::quit();
    });
    return QCoreApplication::exec();
}
