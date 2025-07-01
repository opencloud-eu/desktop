/*
 * Copyright (C) by Erik Verbruggen <erik@verbruggen.consulting>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "platform.h"

#if defined(Q_OS_WIN)
#include "platform_win.h"
#elif defined(Q_OS_MACOS)
#include "platform_mac.h"
#else
#include "platform_unix.h"
#endif

#include "configfile.h"


#ifdef CRASHREPORTER_EXECUTABLE
#include <QDir>
#include <libcrashreporter-handler/Handler.h>
#endif

#include <QApplication>
#include <QFontDatabase>

namespace OCC {

Platform::Platform(Type t)
    : _type(t)
{
}

void Platform::setApplication([[maybe_unused]] QCoreApplication *application)
{
    if (qobject_cast<QApplication *>(application)) {
        QFontDatabase::addApplicationFont(QStringLiteral(":/client/font-awesome/Font Awesome 6 Free-Solid-900.otf"));
        QFontDatabase::addApplicationFont(QStringLiteral(":/client/remixicon/remixicon.ttf"));
    }
#ifdef CRASHREPORTER_EXECUTABLE
    if (ConfigFile().crashReporter()) {
        auto *crashHandler =
            new CrashReporter::Handler(QDir::tempPath(), true, QStringLiteral("%1/" CRASHREPORTER_EXECUTABLE).arg(application->applicationDirPath()));
        connect(application, &QCoreApplication::aboutToQuit, this, [crashHandler] { delete crashHandler; });
    }
#endif
}

void Platform::startServices() { }

Platform::Type Platform::type() const
{
    return _type;
}

std::unique_ptr<Platform> Platform::create(Type t)
{
    // we need to make sure the platform class is initialized before a Q(Core)Application has been set up
    // the constructors run some initialization code that affects Qt's initialization
    Q_ASSERT(QCoreApplication::instance() == nullptr);

    return std::unique_ptr<Platform>{
#if defined(Q_OS_WIN)
        new WinPlatform(t)
#elif defined(Q_OS_LINUX)
        new UnixPlatform(t)
#elif defined(Q_OS_MAC)
        new MacPlatform(t)
#else
#error Unsupported platform
#endif
    };
}

} // OCC namespace
