/* This software is in the public domain, furnished "as is", without warranty. */

#include "gui/accountmanager.h"
#include "gui/accountstate.h"
#include "gui/folderman.h"
#include "libsync/account.h"
#include "libsync/configfile.h"
#include "libsync/creds/httpcredentials.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace OCC;

class HttpCredentialsTest : public HttpCredentials
{
public:
    using HttpCredentials::HttpCredentials;
    void restartOauth() override { }
};

class TestSyncProviderSelection : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init() { ConfigFile::makeQSettings().clear(); }

    void testPreferenceAppliesOnRestart()
    {
        QVERIFY(!ConfigFile().traditionalFolderSync());
        auto manager = FolderMan::createInstance(true);
        ConfigFile().setTraditionalFolderSync(true);
        QVERIFY(ConfigFile().traditionalFolderSync());
        QVERIFY(manager->useFileProvider());
        manager->setSyncEnabled(true);
        QVERIFY(manager->folders().isEmpty());
        manager.reset();

        manager = FolderMan::createInstance(false);
        QVERIFY(!manager->useFileProvider());
        manager->setSyncEnabled(true);
        ConfigFile().setTraditionalFolderSync(false);
        QVERIFY(!ConfigFile().traditionalFolderSync());
        QVERIFY(!manager->useFileProvider());
    }

    void testOnDemandPreservesInactiveFolders()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        FolderDefinition definition(QUuid::createUuid(), QUrl(QStringLiteral("https://example.org/dav")), {}, QStringLiteral("Documents"));
        definition.setLocalPath(dir.path());
        definition.journalPath = QStringLiteral("saved-journal.db");
        definition.paused = true;
        definition.setPriority(42);
        {
            auto settings = ConfigFile::makeQSettings();
            settings.beginWriteArray("Folders", 1);
            settings.setArrayIndex(0);
            FolderDefinition::save(settings, definition);
            settings.endArray();
        }
        QFile localFile(dir.filePath(QStringLiteral("local.txt")));
        QVERIFY(localFile.open(QIODevice::WriteOnly));
        localFile.write("local changes");
        localFile.close();

        const auto before = ConfigFile::makeQSettings();
        QMap<QString, QVariant> saved;
        for (const auto &key : before.allKeys()) {
            saved.insert(key, before.value(key));
        }
        {
            auto manager = FolderMan::createInstance(true);
            QCOMPARE(manager->loadFolders().value(), 0);
            QVERIFY(manager->folders().isEmpty());
            QVERIFY(!manager->addFolder({}, definition));
            auto newDefinition = definition;
            newDefinition.setLocalPath(dir.filePath(QStringLiteral("must-not-be-created")));
            QVERIFY(!manager->addFolderFromWizard({}, std::move(newDefinition), false));
            QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("must-not-be-created"))));
            manager->setIgnoreHiddenFiles(false);
            manager->unloadAndDeleteAllFolders();
        }
        const auto after = ConfigFile::makeQSettings();
        QCOMPARE(after.allKeys(), saved.keys());
        for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
            QCOMPARE(after.value(it.key()), it.value());
        }
        QVERIFY(localFile.open(QIODevice::ReadOnly));
        QCOMPARE(localFile.readAll(), QByteArray("local changes"));
    }

    void testShutdownPreservesInactiveFolders()
    {
        auto manager = FolderMan::createInstance(true);
        auto account = Account::create(QUuid::createUuid());
        account->setCredentials(new HttpCredentialsTest(QStringLiteral("secret")));
        QVERIFY(AccountManager::instance()->addAccount(account));
        FolderDefinition definition(account->uuid(), QUrl(QStringLiteral("https://example.org/dav")), {}, {});
        definition.setLocalPath(QStringLiteral("/saved/folder"));
        {
            auto settings = ConfigFile::makeQSettings();
            settings.beginWriteArray("Folders", 1);
            settings.setArrayIndex(0);
            FolderDefinition::save(settings, definition);
            settings.endArray();
        }
        QSignalSpy removed(AccountManager::instance(), &AccountManager::accountRemoved);
        QSignalSpy deleted(AccountManager::instance(), &AccountManager::accountDeleted);
        AccountManager::instance()->shutdown();
        QCOMPARE(removed.count(), 1);
        QCOMPARE(deleted.count(), 0);
        auto settings = ConfigFile::makeQSettings();
        QCOMPARE(settings.beginReadArray("Folders"), 1);
        settings.setArrayIndex(0);
        QCOMPARE(FolderDefinition::load(settings).accountUUID(), account->uuid());
        settings.endArray();
    }

    void testRemoveAccountWithInactiveFolders()
    {
        auto manager = FolderMan::createInstance(true);
        auto newAccount = Account::create(QUuid::createUuid());
        newAccount->setCredentials(new HttpCredentialsTest(QStringLiteral("secret")));
        const auto account = AccountManager::instance()->addAccount(newAccount);
        const auto otherAccount = QUuid::createUuid();
        {
            auto settings = ConfigFile::makeQSettings();
            settings.beginWriteArray("Folders", 3);
            for (int i = 0; i < 3; ++i) {
                settings.setArrayIndex(i);
                FolderDefinition definition(i == 1 ? otherAccount : account->account()->uuid(), QUrl(QStringLiteral("https://example.org/dav")), {}, {});
                definition.setLocalPath(QStringLiteral("/saved/folder%1").arg(i));
                FolderDefinition::save(settings, definition);
                settings.setValue(QStringLiteral("virtualFilesMode"), QStringLiteral("future-provider"));
                settings.setValue(QStringLiteral("provider/customState"), QByteArray("preserve inactive state"));
            }
            settings.endArray();
        }
        AccountManager::instance()->deleteAccount(account);
        auto settings = ConfigFile::makeQSettings();
        QCOMPARE(settings.beginReadArray("Folders"), 1);
        settings.setArrayIndex(0);
        QCOMPARE(settings.value(QStringLiteral("accountUUID")).toUuid(), otherAccount);
        QCOMPARE(settings.value(QStringLiteral("localPath")).toString(), QStringLiteral("/saved/folder1/"));
        QCOMPARE(settings.value(QStringLiteral("virtualFilesMode")).toString(), QStringLiteral("future-provider"));
        QCOMPARE(settings.value(QStringLiteral("provider/customState")).toByteArray(), QByteArray("preserve inactive state"));
        settings.endArray();
    }
};

QTEST_GUILESS_MAIN(TestSyncProviderSelection)
#include "testsyncproviderselection.moc"
