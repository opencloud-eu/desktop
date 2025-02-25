/*
 * Copyright (C) by Olivier Goffart <ogoffart@owncloud.com>
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
 * Copyright (C) by Daniel Heule <daniel.heule@gmail.com>
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

#include "account.h"
#include "cmd/tokencredentials.h"
#include "common/syncjournaldb.h"
#include "common/version.h"
#include "configfile.h" // ONLY ACCESS THE STATIC FUNCTIONS!
#include "libsync/logger.h"
#include "libsync/theme.h"
#include "networkjobs/checkserverjobfactory.h"
#include "networkjobs/jsonjob.h"
#include "platform.h"
#include "syncengine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QUrl>

#include <iostream>
#include <memory>
#include <random>


using namespace OCC;

namespace {

struct CmdOptions
{
    QString source_dir;
    QUrl target_url;
    QUrl server_url;
    QString remoteFolder;

    QByteArray username;
    QByteArray token;

    QString proxy;
    bool silent = false;
    bool trustSSL = false;
    bool interactive = true;
    bool ignoreHiddenFiles = true;
    QString exclude;
    QString unsyncedfolders;
    int restartTimes = 3;
    int downlimit = 0;
    int uplimit = 0;
};

struct SyncCTX
{
    explicit SyncCTX(const CmdOptions &cmdOptions)
        : options{cmdOptions}
        , promptRemoveAllFiles(cmdOptions.interactive)
    {
    }
    CmdOptions options;
    bool promptRemoveAllFiles;
    AccountPtr account;
};

/* If the selective sync list is different from before, we need to disable the read from db
  (The normal client does it in SelectiveSyncDialog::accept*)
 */
void selectiveSyncFixup(OCC::SyncJournalDb *journal, const QSet<QString> &newListSet)
{
    SqlDatabase db;
    if (!db.openOrCreateReadWrite(journal->databaseFilePath())) {
        return;
    }

    bool ok;

    const auto oldBlackListSet = journal->getSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, &ok);
    if (ok) {
        const auto changes = (oldBlackListSet - newListSet) + (newListSet - oldBlackListSet);
        for (const auto &it : changes) {
            journal->schedulePathForRemoteDiscovery(it);
        }

        journal->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, newListSet);
    }
}


void sync(const SyncCTX &ctx)
{
    const auto selectiveSyncList = [&]() -> QSet<QString> {
        if (!ctx.options.unsyncedfolders.isEmpty()) {
            QFile f(ctx.options.unsyncedfolders);
            if (!f.open(QFile::ReadOnly)) {
                qCritical() << "Could not open file containing the list of unsynced folders: " << ctx.options.unsyncedfolders;
            } else {
                // filter out empty lines and comments
                auto selectiveSyncList = QString::fromUtf8(f.readAll())
                                             .split(QLatin1Char('\n'))
                                             .filter(QRegularExpression(QStringLiteral("\\S+")))
                                             .filter(QRegularExpression(QStringLiteral("^[^#]")));

                for (int i = 0; i < selectiveSyncList.count(); ++i) {
                    if (!selectiveSyncList.at(i).endsWith(QLatin1Char('/'))) {
                        selectiveSyncList[i].append(QLatin1Char('/'));
                    }
                }
                return {selectiveSyncList.cbegin(), selectiveSyncList.cend()};
            }
        }
        return {};
    }();

    const QString dbPath = ctx.options.source_dir + SyncJournalDb::makeDbName(ctx.options.source_dir);
    auto db = new SyncJournalDb(dbPath, qApp);
    if (!selectiveSyncList.empty()) {
        selectiveSyncFixup(db, selectiveSyncList);
    }

    SyncOptions opt{QSharedPointer<Vfs>(VfsPluginManager::instance().createVfsFromPlugin(Vfs::Off).release())};
    auto engine = new SyncEngine(
        ctx.account, ctx.options.target_url, ctx.options.source_dir, ctx.options.remoteFolder, db);
    engine->setSyncOptions(opt);
    engine->setParent(db);

    QObject::connect(engine, &SyncEngine::finished, engine, [engine, ctx, restartCount = std::make_shared<int>(0)](bool result) {
        if (!result) {
            qWarning() << "Failed to sync";
            exit(EXIT_FAILURE);
        } else {
            if (engine->isAnotherSyncNeeded()) {
                if (*restartCount < ctx.options.restartTimes) {
                    (*restartCount)++;
                    qDebug() << "Restarting Sync, because another sync is needed" << *restartCount;
                    engine->startSync();
                    return;
                }
                qWarning() << "Another sync is needed, but not done because restart count is exceeded" << *restartCount;
            } else {
                qApp->quit();
            }
        }
    });
    QObject::connect(engine, &SyncEngine::syncError, engine,
        [](const QString &error) { qWarning() << "Sync error:" << error; });
    engine->setIgnoreHiddenFiles(ctx.options.ignoreHiddenFiles);
    engine->setNetworkLimits(ctx.options.uplimit, ctx.options.downlimit);


    // Exclude lists

    bool hasUserExcludeFile = !ctx.options.exclude.isEmpty();
    QString systemExcludeFile = ConfigFile::excludeFileFromSystem();

    // Always try to load the user-provided exclude list if one is specified
    if (hasUserExcludeFile) {
        engine->addExcludeList(ctx.options.exclude);
    }
    // Load the system list if available, or if there's no user-provided list
    if (!hasUserExcludeFile || QFile::exists(systemExcludeFile)) {
        engine->addExcludeList(systemExcludeFile);
    }

    if (!engine->reloadExcludes()) {
        qCritical() << "Cannot load system exclude list or list supplied via --exclude";
        exit(EXIT_FAILURE);
    }
    engine->startSync();
}

void setupCredentials(SyncCTX &ctx)
{
    // Order of retrieval attempt (later attempts override earlier ones):
    // 1. From URL
    // 2. From options
    // 3. From prompt (if interactive)

    if (!ctx.options.proxy.isNull()) {
        QString host;
        uint32_t port = 0;
        bool ok;

        QStringList pList = ctx.options.proxy.split(QLatin1Char(':'));
        if (pList.count() == 3) {
            // http: //192.168.178.23 : 8080
            //  0            1            2
            host = pList.at(1);
            if (host.startsWith(QLatin1String("//")))
                host.remove(0, 2);

            port = pList.at(2).toUInt(&ok);
            if (!ok || port > std::numeric_limits<uint16_t>::max()) {
                qCritical() << "Invalid port number";
                exit(EXIT_FAILURE);
            }

            QNetworkProxyFactory::setUseSystemConfiguration(false);
            QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, host, static_cast<uint16_t>(port)));
        } else {
            qCritical() << "Could not read httpproxy. The proxy should have the format \"http://hostname:port\".";
            exit(EXIT_FAILURE);
        }
    }

    // Pre-flight check: verify that the file specified by --unsyncedfolders can be read by us:
    if (!ctx.options.unsyncedfolders.isNull()) { // yes, isNull and not isEmpty because...:
        // ... if the user entered "--unsyncedfolders ''" on the command-line, opening that will
        // also fail
        QFile f(ctx.options.unsyncedfolders);
        if (!f.open(QFile::ReadOnly)) {
            qCritical() << "Cannot read unsyncedfolders file '" << ctx.options.unsyncedfolders << "': " << f.errorString();
            exit(EXIT_FAILURE);
        }
        f.close();
    }

    ctx.account->setCredentials(new TokenCredentials(std::move(ctx.options.username), std::move(ctx.options.token)));
    if (ctx.options.trustSSL) {
        QObject::connect(ctx.account->accessManager(), &QNetworkAccessManager::sslErrors, qApp,
            [](QNetworkReply *reply, const QList<QSslError> &errors) { reply->ignoreSslErrors(errors); });
    } else {
        QObject::connect(ctx.account->accessManager(), &QNetworkAccessManager::sslErrors, qApp, [](QNetworkReply *reply, const QList<QSslError> &errors) {
            Q_UNUSED(reply)

            qCritical() << "SSL error encountered";
            for (const auto &e : errors) {
                qCritical() << e.errorString();
            }
            qCritical() << "If you trust the certificate and want to ignore the errors, use the --trust option.";
            exit(EXIT_FAILURE);
        });
    }
}
}

CmdOptions parseOptions(const QStringList &app_args)
{
    CmdOptions options;
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("%1 version %2 - command line client tool").arg(QCoreApplication::instance()->applicationName(), OCC::Version::displayString()));

    // this little snippet saves a few lines below
    auto addOption = [&parser](QCommandLineOption &&option, std::optional<QCommandLineOption::Flag> &&flags = {}) {
        if (flags.has_value()) {
            option.setFlags(flags.value());
        }
        parser.addOption(option);
        return option;
    };

    auto serverOption = addOption({{QStringLiteral("server")}, QStringLiteral("The URL for the server"), QStringLiteral("url")});
    auto userOption = addOption({{QStringLiteral("u"), QStringLiteral("user")}, QStringLiteral("Username"), QStringLiteral("name")});
    auto tokenOption = addOption({{QStringLiteral("t"), QStringLiteral("token")}, QStringLiteral("Authentication token"), QStringLiteral("token")});

    auto silentOption = addOption({ { QStringLiteral("s"), QStringLiteral("silent") }, QStringLiteral("Don't be so verbose.") });
    auto httpproxyOption = addOption({ { QStringLiteral("httpproxy") }, QStringLiteral("Specify a http proxy to use."), QStringLiteral("http://server:port") });
    auto trustOption = addOption({ { QStringLiteral("trust") }, QStringLiteral("Trust the SSL certification") });
    auto excludeOption = addOption({ { QStringLiteral("exclude") }, QStringLiteral("Path to an exclude list [file]"), QStringLiteral("file") });
    auto unsyncedfoldersOption = addOption({ { QStringLiteral("unsyncedfolders") }, QStringLiteral("File containing the list of unsynced remote folders (selective sync)"), QStringLiteral("file") });

    auto nonInterActiveOption = addOption({ { QStringLiteral("non-interactive") }, QStringLiteral("Do not block execution with interaction") });
    auto maxRetriesOption = addOption({ { QStringLiteral("max-sync-retries") }, QStringLiteral("Retries maximum n times (default to 3)"), QStringLiteral("n") });
    auto uploadLimitOption = addOption({ { QStringLiteral("uplimit") }, QStringLiteral("Limit the upload speed of files to n KB/s"), QStringLiteral("n") });
    auto downloadLimitption = addOption({ { QStringLiteral("downlimit") }, QStringLiteral("Limit the download speed of files to n KB/s"), QStringLiteral("n") });
    auto syncHiddenFilesOption = addOption({ { QStringLiteral("sync-hidden-files") }, QStringLiteral("Enables synchronization of hidden files") });

    auto logdebugOption = addOption({ { QStringLiteral("logdebug") }, QStringLiteral("More verbose logging") });

    const auto testCrashReporter =
        addOption({{QStringLiteral("crash")}, QStringLiteral("Crash the client to test the crash reporter")}, QCommandLineOption::HiddenFromHelp);

    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument(QStringLiteral("source_dir"), QStringLiteral("The source dir"));
    parser.addPositionalArgument(QStringLiteral("space_url"), QStringLiteral("The URL to the space"));
    parser.addPositionalArgument(QStringLiteral("remote_folder"), QStringLiteral("A remote folder"), QStringLiteral("[remote folder]"));

    parser.process(app_args);


    const QStringList args = parser.positionalArguments();
    if (args.size() < 2 || args.size() > 3) {
        parser.showHelp(EXIT_FAILURE);
    }

    options.source_dir = [&parser, arg = args[0]] {
        const QFileInfo fi(arg);
        if (!fi.exists()) {
            qCritical() << "Source dir" << arg << "does not exist.";
            parser.showHelp(EXIT_FAILURE);
        }
        QString sourceDir = fi.absoluteFilePath();
        if (!sourceDir.endsWith(QLatin1Char('/'))) {
            sourceDir.append(QLatin1Char('/'));
        }
        return sourceDir;
    }();
    options.target_url = QUrl::fromUserInput(args[1]);
    if (args.size() == 3) {
        options.remoteFolder = args[2];
    }

    if (parser.isSet(httpproxyOption)) {
        options.proxy = parser.value(httpproxyOption);
    }
    if (parser.isSet(silentOption)) {
        options.silent = true;
    }
    if (parser.isSet(trustOption)) {
        options.trustSSL = true;
    }
    if (parser.isSet(nonInterActiveOption)) {
        options.interactive = false;
    }
    if (parser.isSet(serverOption)) {
        options.server_url = QUrl::fromUserInput(parser.value(serverOption));
    } else {
        qCritical() << "Server not set";
        parser.showHelp(EXIT_FAILURE);
    }
    if (parser.isSet(tokenOption)) {
        options.token = parser.value(tokenOption).toUtf8();
    } else {
        qCritical() << "Token not set";
        parser.showHelp(EXIT_FAILURE);
    }
    if (parser.isSet(userOption)) {
        options.username = parser.value(userOption).toUtf8();
    } else {
        qCritical() << "Username not set";
        parser.showHelp(EXIT_FAILURE);
    }
    if (parser.isSet(excludeOption)) {
        options.exclude = parser.value(excludeOption);
    }
    if (parser.isSet(unsyncedfoldersOption)) {
        options.unsyncedfolders = parser.value(unsyncedfoldersOption);
    }
    if (parser.isSet(maxRetriesOption)) {
        options.restartTimes = parser.value(maxRetriesOption).toInt();
    }
    if (parser.isSet(uploadLimitOption)) {
        options.uplimit = parser.value(maxRetriesOption).toInt() * 1000;
    }
    if (parser.isSet(downloadLimitption)) {
        options.downlimit = parser.value(downloadLimitption).toInt() * 1000;
    }
    if (parser.isSet(syncHiddenFilesOption)) {
        options.ignoreHiddenFiles = false;
    }
    if (parser.isSet(logdebugOption)) {
        Logger::instance()->setLogFile(QStringLiteral("-"));
        Logger::instance()->setLogDebug(true);
    }
    if (parser.isSet(testCrashReporter)) {
        // crash onc ethe main loop was started
        qCritical() << "We'll soon crash on purpose";
        QTimer::singleShot(0, qApp, &Utility::crash);
    }
    return options;
}

int main(int argc, char **argv)
{
    auto platform = OCC::Platform::create();

    QCoreApplication app(argc, argv);

    platform->migrate();
    platform->setApplication(&app);

    app.setApplicationVersion(Theme::instance()->versionSwitchOutput());

    SyncCTX ctx { parseOptions(app.arguments()) };

    // start the main loop before we ask for the username etc
    QTimer::singleShot(0, &app, [&] {
        if (ctx.options.silent) {
            qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
        } else {
            qSetMessagePattern(Logger::loggerPattern());
        }

        ctx.account = Account::create(QUuid::createUuid());

        if (!ctx.account) {
            qCritical() << "Could not initialize account!";
            exit(EXIT_FAILURE);
        }

        setupCredentials(ctx);

        // don't leak credentials more than needed
        ctx.options.server_url = ctx.options.server_url.adjusted(QUrl::RemoveUserInfo);
        ctx.options.target_url = ctx.options.target_url.adjusted(QUrl::RemoveUserInfo);


        ctx.account->setUrl(ctx.options.server_url);

        auto *checkServerJob = CheckServerJobFactory(ctx.account->accessManager()).startJob(ctx.account->url(), qApp);

        QObject::connect(checkServerJob, &CoreJob::finished, qApp, [ctx, checkServerJob] {
            if (checkServerJob->success()) {
                // Perform a call to get the capabilities.
                auto *capabilitiesJob = new JsonApiJob(ctx.account, QStringLiteral("ocs/v1.php/cloud/capabilities"), {}, {}, nullptr);
                QObject::connect(capabilitiesJob, &JsonApiJob::finishedSignal, qApp, [capabilitiesJob, ctx] {
                    if (capabilitiesJob->reply()->error() != QNetworkReply::NoError || capabilitiesJob->httpStatusCode() != 200) {
                        qCritical() << "Error connecting to server";
                        exit(EXIT_FAILURE);
                    }
                    auto caps = capabilitiesJob->data()
                                    .value(QStringLiteral("ocs"))
                                    .toObject()
                                    .value(QStringLiteral("data"))
                                    .toObject()
                                    .value(QStringLiteral("capabilities"))
                                    .toObject();
                    qDebug() << "Server capabilities" << caps;
                    ctx.account->setCapabilities({ctx.account->url(), caps.toVariantMap()});

                    switch (ctx.account->serverSupportLevel()) {
                    case Account::ServerSupportLevel::Supported:
                        break;
                    case Account::ServerSupportLevel::Unknown:
                        qWarning() << "Failed to detect server version";
                        break;
                    case Account::ServerSupportLevel::Unsupported:
                        qCritical() << "Error unsupported server";
                        exit(EXIT_FAILURE);
                    }

                    // much lower age than the default since this utility is usually made to be run right after a change in the tests
                    SyncEngine::minimumFileAgeForUpload = std::chrono::seconds(0);
                    sync(ctx);
                });
                capabilitiesJob->start();
            } else {
                if (checkServerJob->reply()->error() == QNetworkReply::OperationCanceledError) {
                    qCritical() << "Looking up " << ctx.account->url().toString() << " timed out.";
                } else {
                    qCritical() << "Failed to resolve " << ctx.account->url().toString() << " Error: " << checkServerJob->reply()->errorString();
                }
                exit(EXIT_FAILURE);
            }
        });
    });

    return app.exec();
}
