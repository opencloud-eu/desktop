
#pragma once

#include <QVariant>
#include <qt_windows.h>

namespace OCC {
namespace Utility {

    bool registryKeyExists(HKEY hRootKey, const QString &subKey);
    QVariant registryGetKeyValue(HKEY hRootKey, const QString &subKey, const QString &valueName);
    bool registrySetKeyValue(HKEY hRootKey, const QString &subKey, const QString &valueName, DWORD type, const QVariant &value);
    bool registryDeleteKeyTree(HKEY hRootKey, const QString &subKey);
    bool registryDeleteKeyValue(HKEY hRootKey, const QString &subKey, const QString &valueName);
    bool registryWalkSubKeys(HKEY hRootKey, const QString &subKey, const std::function<void(HKEY, const QString &)> &callback);
    bool registryWalkValues(HKEY hRootKey, const QString &subKey, const std::function<void(const QString &, bool *)> &callback);
}
}
