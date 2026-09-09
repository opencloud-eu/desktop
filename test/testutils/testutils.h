#pragma once

#include "account.h"
#include "common/checksumalgorithms.h"
#include "folder.h"
#include "folderman.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <memory>

namespace OCC {

namespace TestUtils {
    Q_NAMESPACE

    // Helper enum to trest enum related utilities
    enum class TestFlag {
        None = 0,
        Flag0 = 0x1 << 0,
        Flag1 = 0x1 << 1,
        // Flag2 is omitted on purpose
        Flag3 = 0x1 << 3
    };
    Q_FLAG_NS(TestFlag)
    Q_DECLARE_FLAGS(TestFlags, TestFlag)

    namespace TestUtilsPrivate {
        void accountStateDeleter(OCC::AccountState *acc);

        using AccountStateRaii = std::unique_ptr<AccountState, decltype(&TestUtilsPrivate::accountStateDeleter)>;
    }

    FolderMan *folderMan();
    FolderDefinition createDummyFolderDefinition(const AccountPtr &acc, const QString &path);
    TestUtilsPrivate::AccountStateRaii createDummyAccount();
    bool writeRandomFile(const QString &fname, int size = -1);

    /***
     * Create a QTemporaryDir with a test specific name pattern
     * OpenCloud-unit-test-{TestName}-XXXXXX
     * This allows to clean up after failed tests
     */
    QTemporaryDir createTempDir();

    const QVariantMap testCapabilities(CheckSums::Algorithm algo = CheckSums::Algorithm::DUMMY_FOR_TESTS);


    QByteArray getPayload(QAnyStringView payloadName);

    // we can't use QMap direclty with QFETCH
    using Values = QMap<QAnyStringView, QAnyStringView>;
    QByteArray getPayloadTemplated(QAnyStringView payloadName, const Values &values);

    // creates a SyncFileItem that fulfills the minimal criteria to not trigger an assert
    SyncFileItem dummyItem(const QString &name);

    QUrl dummyDavUrl();
}
}
