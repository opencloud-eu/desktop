#include "testutils.h"

#include "common/checksums.h"
#include "gui/accountmanager.h"
#include "libsync/creds/httpcredentials.h"
#include "resources/template.h"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QTest>

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
        HttpCredentialsTest *cred = new HttpCredentialsTest(QStringLiteral("secret"));
        acc->setCredentials(cred);
        acc->setUrl(QUrl(QStringLiteral("http://localhost/")));
        acc->setDavDisplayName(QStringLiteral("fakename") + acc->uuid().toString(QUuid::WithoutBraces));
        acc->setCapabilities({acc->url(), OCC::TestUtils::testCapabilities()});
        return {OCC::AccountManager::instance()->addAccount(acc).get(), &TestUtilsPrivate::accountStateDeleter};
    }

    FolderDefinition createDummyFolderDefinition(const AccountPtr &acc, const QString &path)
    {
        auto d = OCC::FolderDefinition(acc->uuid(), Utility::concatUrlPath(dummyDavUrl(), path), {}, QStringLiteral("Dummy Folder"));
        d.setLocalPath(path);
        return d;
    }

    QTemporaryDir createTempDir()
    {
        return QTemporaryDir{QStringLiteral("%1/OpenCloud-unit-test-%2-XXXXXX").arg(QDir::tempPath(), qApp->applicationName())};
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
        return {{QStringLiteral("core"),
                    QVariantMap{{QStringLiteral("status"),
                        QVariantMap{{QStringLiteral("installed"), QStringLiteral("1")}, {QStringLiteral("maintenance"), QStringLiteral("0")},
                            {QStringLiteral("needsDbUpgrade"), QStringLiteral("0")}, {QStringLiteral("version"), QStringLiteral("10.11.0.0")},
                            {QStringLiteral("versionstring"), QStringLiteral("10.11.0")}, {QStringLiteral("edition"), QStringLiteral("Community")},
                            {QStringLiteral("productname"), QStringLiteral("OpenCloud")}, {QStringLiteral("product"), QStringLiteral("OpenCloud")},
                            {QStringLiteral("productversion"), QStringLiteral("2.0.0-beta1+7c2e3201b")}}}}},
            {QStringLiteral("files"), QVariantList{}}, {QStringLiteral("dav"), QVariantMap{{QStringLiteral("chunking"), QStringLiteral("1.0")}}},
            {QStringLiteral("checksums"),
                QVariantMap{{QStringLiteral("preferredUploadType"), Utility::enumToString(algo)}, {QStringLiteral("supportedTypes"), algorithmNames}}}};
    }

    QByteArray getPayload(QAnyStringView payloadName)
    {
        static QFileInfo info(QString::fromUtf8(QTest::currentAppName()));
        QFile f(QStringLiteral(SOURCEDIR "/test/%1/%2").arg(info.baseName(), payloadName.toString()));
        if (!f.open(QIODevice::ReadOnly)) {
            qFatal() << "Failed to open file: " << f.fileName();
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
        return QUrl(QStringLiteral("http://localhost/dav/spaces/0e443965-2ebb-4673-9464-b2c1d388e666$cb867555-fdf7-48ce-8f1c-d64570812f21"));
    }

    void TestUtilsPrivate::accountStateDeleter(OCC::AccountState *acc)
    {
        if (acc) {
            OCC::AccountManager::instance()->deleteAccount(acc);
        }
    }
}
}
