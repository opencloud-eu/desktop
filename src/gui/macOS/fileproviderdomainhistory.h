#pragma once

#include "libsync/configfile.h"
#include <QStringList>

namespace OCC::Mac {

inline void rememberFileProviderDomains(const QStringList &domains)
{
    auto settings = ConfigFile::makeQSettings();
    settings.setValue(QStringLiteral("FileProvider/DomainHistoryKnown"), true);
    settings.setValue(QStringLiteral("FileProvider/RegisteredDomains"), domains);
}

inline void rememberActiveFileProviderDomain(const QString &domain)
{
    auto settings = ConfigFile::makeQSettings();
    auto domains = settings.value(QStringLiteral("FileProvider/RegisteredDomains")).toStringList();
    if (!domains.contains(domain)) {
        domains.append(domain);
    }
    settings.setValue(QStringLiteral("FileProvider/RegisteredDomains"), domains);
    settings.setValue(QStringLiteral("FileProvider/AllDomainsDisconnected"), false);
}

inline bool canStartFolderSyncWithoutFileProvider()
{
    auto settings = ConfigFile::makeQSettings();
    if (settings.value(QStringLiteral("FileProvider/AllDomainsDisconnected"), false).toBool()) {
        return true;
    }
    if (!settings.value(QStringLiteral("FileProvider/RegisteredDomains")).toStringList().isEmpty()) {
        return false;
    }
    if (settings.value(QStringLiteral("FileProvider/DomainHistoryKnown"), false).toBool()) {
        return true;
    }
    // Older installations have no domain ledger. Saved accounts mean we cannot
    // establish that their on-demand domains were disconnected successfully.
    return settings.value(QStringLiteral("Accounts/size"), 0).toInt() == 0;
}

}
