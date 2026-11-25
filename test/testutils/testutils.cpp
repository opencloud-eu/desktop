#include "testutils.h"

#include "common/checksums.h"
#include "gui/accountmanager.h"
#include "libsync/creds/httpcredentials.h"
#include "resources/template.h"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {
class HttpCredentialsTest : public OCC::HttpCredentials
{
public:
    HttpCredentialsTest(const QString &password)
        : HttpCredentials(password)
    {
    }

    void restartOauth() override { }
};
}

namespace OCC {

namespace TestUtils {
    TestUtilsPrivate::AccountStateRaii createDummyAccount()
    {
        // ensure we have an instance of folder man
        std::ignore = folderMan();
        // don't use the account manager to create the account, it would try to use widgets
        auto acc = Account::create(QUuid::createUuid());
        HttpCredentialsTest *cred = new HttpCredentialsTest("secret"_L1);
        acc->setCredentials(cred);
        acc->setUrl(QUrl("http://localhost/"_L1));
        acc->setDavDisplayName("fakename"_L1 + acc->uuid().toString(QUuid::WithoutBraces));
        acc->setCapabilities({acc->url(), OCC::TestUtils::testCapabilities()});
        return {OCC::AccountManager::instance()->addAccount(acc).get(), &TestUtilsPrivate::accountStateDeleter};
    }

    FolderDefinition createDummyFolderDefinition(const AccountPtr &acc, const QString &path)
    {
        auto d = OCC::FolderDefinition(acc->uuid(), Utility::concatUrlPath(dummyDavUrl(), path), {}, "Dummy Folder"_L1);
        d.setLocalPath(path);
        return d;
    }

    QTemporaryDir createTempDir()
    {
        return QTemporaryDir{"%1/OpenCloud-unit-test-%2-XXXXXX"_L1.arg(QDir::tempPath(), qApp->applicationName())};
    }

    FolderMan *folderMan()
    {
        static QPointer<FolderMan> man;
        if (!man) {
            man = FolderMan::createInstance().release();
            QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, man, &FolderMan::deleteLater);
        };
        return man;
    }


    bool writeRandomFile(const QString &fname, int size)
    {
        auto rg = QRandomGenerator::global();

        const int maxSize = 10 * 10 * 1024;
        if (size == -1) {
            size = static_cast<int>(rg->generate() % maxSize);
        }

        QString randString;
        for (int i = 0; i < size; i++) {
            int r = static_cast<int>(rg->generate() % 128);
            randString.append(QChar(r));
        }

        QFile file(fname);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << randString;
            // optional, as QFile destructor will already do it:
            file.close();
            return true;
        }
        return false;
    }

    const QVariantMap testCapabilities(CheckSums::Algorithm algo)
    {
        static const auto algorithmNames = [] {
            QVariantList out;
            for (const auto &a : CheckSums::All) {
                out.append(Utility::enumToString(a.first));
            }
            return out;
        }();
        // clang-format off
        return {{"core"_L1,
                    QVariantMap{{"status"_L1,
                        QVariantMap{{"installed"_L1, "1"_L1}, {"maintenance"_L1, "0"_L1},
                            {"needsDbUpgrade"_L1, "0"_L1}, {"version"_L1, "10.11.0.0"_L1},
                            {"versionstring"_L1, "10.11.0"_L1}, {"edition"_L1, "Community"_L1},
                            {"productname"_L1, "OpenCloud"_L1}, {"product"_L1, "OpenCloud"_L1},
                            {"productversion"_L1, "2.0.0-beta1+7c2e3201b"_L1}}}}},
            {"files"_L1, QVariantList{}}, {"dav"_L1, QVariantMap{{"chunking"_L1, "1.0"_L1}}},
            {"checksums"_L1,
                QVariantMap{{"preferredUploadType"_L1, Utility::enumToString(algo)}, {"supportedTypes"_L1, algorithmNames}}}};
        // clang-format on
    }

    QByteArray getPayload(QAnyStringView payloadName)
    {
        static QFileInfo info(QString::fromUtf8(QTest::currentAppName()));
        QFile f(QStringLiteral(SOURCEDIR "/test/%1/%2").arg(info.baseName(), payloadName.toString()));
        if (!f.open(QIODevice::ReadOnly)) {
            qFatal() << u"Failed to open file: " << f.fileName();
        }
        return f.readAll();
    }

    QByteArray getPayloadTemplated(QAnyStringView payloadName, const Values &values)
    {
        return Resources::Template::renderTemplate(QString::fromUtf8(getPayload(payloadName)), values).toUtf8();
    }

    SyncFileItem dummyItem(const QString &name)
    {
        SyncFileItem item(name);
        item._type = ItemTypeFile;
        item._fileId = "id";
        item._inode = 1;
        item._modtime = Utility::qDateTimeToTime_t(QDateTime::currentDateTimeUtc());
        item._remotePerm = RemotePermissions::fromDbValue(" ");
        return item;
    }

    QUrl dummyDavUrl()
    {
        return QUrl("http://localhost/dav/spaces/0e443965-2ebb-4673-9464-b2c1d388e666$cb867555-fdf7-48ce-8f1c-d64570812f21"_L1);
    }

    void TestUtilsPrivate::accountStateDeleter(OCC::AccountState *acc)
    {
        if (acc) {
            OCC::AccountManager::instance()->deleteAccount(acc);
        }
    }
}
}
