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
#include "libsync/graphapi/spacesmanager.h"
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

#include <zlib.h>


using namespace OCC;

namespace {
// start in quiet mode
bool logQuietMode = true;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (std::strcmp(context.category, "default")) {
        if (logQuietMode) {
            return;
        }
        std::cerr << qPrintable(qFormatLogMessage(type, context, message)) << std::endl;
    } else {
        switch (type) {
        case QtInfoMsg:
            std::cout << qPrintable(message) << std::endl;
            return;
        case QtDebugMsg:
            // ignore debug message if we are in quiet mode
            if (logQuietMode) {
                return;
            }
            std::cerr << "Debug";
            break;
        case QtWarningMsg:
            std::cerr << "Warning";
            break;
        case QtCriticalMsg:
            std::cerr << "Critical";
            break;
        case QtFatalMsg:
            std::cerr << "Fatal: " << qPrintable(message) << std::endl;
            exit(EXIT_FAILURE);
        }
        std::cerr << ": " << qPrintable(message) << std::endl;
    }
}

struct CmdOptions
{
    QString source_dir;
    QString space_id;
    QUrl server_url;
    QString remoteFolder;

    QByteArray username;
    QByteArray token = qgetenv("OPENCLOUD_TOKEN");

    QString proxy;
    bool query = false;
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


void sync(const SyncCTX &ctx, const QUrl &spaceUrl)
{
    const auto selectiveSyncList = [&]() -> QSet<QString> {
        if (!ctx.options.unsyncedfolders.isEmpty()) {
            QFile f(ctx.options.unsyncedfolders);
            if (!f.open(QFile::ReadOnly)) {
                qCritical() << u"Could not open file containing the list of unsynced folders: " << ctx.options.unsyncedfolders;
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
    auto engine = new SyncEngine(ctx.account, spaceUrl, ctx.options.source_dir, ctx.options.remoteFolder, db);
    engine->setSyncOptions(opt);
    engine->setParent(db);

    QObject::connect(engine, &SyncEngine::finished, engine, [engine, ctx, restartCount = std::make_shared<int>(0)](bool result) {
        if (!result) {
            qWarning() << u"Failed to sync";
            exit(EXIT_FAILURE);
        } else {
            if (engine->isAnotherSyncNeeded()) {
                if (*restartCount < ctx.options.restartTimes) {
                    (*restartCount)++;
                    qDebug() << u"Restarting Sync, because another sync is needed" << *restartCount;
                    engine->startSync();
                    return;
                }
                qWarning() << u"Another sync is needed, but not done because restart count is exceeded" << *restartCount;
            } else {
                qInfo() << u"Sync succeeded";
                qApp->quit();
            }
        }
    });
    QObject::connect(engine, &SyncEngine::syncError, engine, [](const QString &error) { qWarning() << u"Sync error:" << error; });
    QObject::connect(engine, &SyncEngine::itemCompleted, engine, [](const SyncFileItemPtr &item) {
        if (item->hasErrorStatus()) {
            switch (item->_status) {
            case SyncFileItem::Excluded:
                qDebug() << u"Sync excluded file:" << item->_errorString;
                break;
            default:
                qWarning() << u"Sync error:" << item->_status << item->_errorString;
            }
        }
    });
    engine->setIgnoreHiddenFiles(ctx.options.ignoreHiddenFiles);
    engine->setNetworkLimits(ctx.options.uplimit, ctx.options.downlimit);


    // Always try to load the user-provided exclude list if one is specified
    if (!ctx.options.exclude.isEmpty()) {
        engine->addExcludeList(ctx.options.exclude);
    } else {
        engine->addExcludeList(ConfigFile::defaultExcludeFile());
    }

    if (!engine->reloadExcludes()) {
        qCritical() << u"Cannot load system exclude list or list supplied via --exclude";
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
                qCritical() << u"Invalid port number";
                exit(EXIT_FAILURE);
            }

            QNetworkProxyFactory::setUseSystemConfiguration(false);
            QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, host, static_cast<uint16_t>(port)));
        } else {
            qCritical() << u"Could not read httpproxy. The proxy should have the format \"http://hostname:port\".";
            exit(EXIT_FAILURE);
        }
    }

    // Pre-flight check: verify that the file specified by --unsyncedfolders can be read by us:
    if (!ctx.options.unsyncedfolders.isNull()) { // yes, isNull and not isEmpty because...:
        // ... if the user entered "--unsyncedfolders ''" on the command-line, opening that will
        // also fail
        QFile f(ctx.options.unsyncedfolders);
        if (!f.open(QFile::ReadOnly)) {
            qCritical() << u"Cannot read unsyncedfolders file '" << ctx.options.unsyncedfolders << u"': " << f.errorString();
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

            qCritical() << u"SSL error encountered";
            for (const auto &e : errors) {
                qCritical() << e.errorString();
            }
            qCritical() << u"If you trust the certificate and want to ignore the errors, use the --trust option.";
            exit(EXIT_FAILURE);
        });
    }
}

QString hashSaceId(const QString &id)
{
    auto adler = adler32_z(0, nullptr, 0);
    adler = adler32_z(adler, reinterpret_cast<Bytef *>(id.toUtf8().data()), id.size());
    return QStringLiteral("space:%1").arg(QString::number(adler, 16));
}

void printSpaces(const QVector<GraphApi::Space *> &spaces)
{
    auto printTable = [](const QString &a, const QString &b, const QString &c) {
        qInfo().noquote() << QStringLiteral("%1 | %2 | %3").arg(a, 15).arg(b, 20).arg(c);
    };
    qInfo() << u"Listing spaces:";
    printTable(QStringLiteral("Short ID"), QStringLiteral("DisplayName"), QStringLiteral("ID"));
    printTable(QString().fill(QLatin1Char('-'), 15), QString().fill(QLatin1Char('-'), 20), QString().fill(QLatin1Char('-'), 20));
    for (auto *s : spaces) {
        printTable(hashSaceId(s->id()), s->displayName(), s->id());
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
    auto userOption = addOption({{QStringLiteral("u"), QStringLiteral("user")}, QStringLiteral("Username"), QStringLiteral("name")});
    auto tokenOption = addOption(
        {{QStringLiteral("t"), QStringLiteral("token")}, QStringLiteral("Authentication token, you can also use $OPENCLOUD_TOKEN"), QStringLiteral("token")});

    auto remoteFolder =
        addOption({{QStringLiteral("remote-folder")}, QStringLiteral("The subdirectory of the space that is synchronized"), QStringLiteral("remote-folder")});
    auto httpproxyOption = addOption({{QStringLiteral("http-proxy")}, QStringLiteral("Specify a http proxy to use"), QStringLiteral("http://server:port")});
    auto trustOption = addOption({ { QStringLiteral("trust") }, QStringLiteral("Trust the SSL certification") });
    auto excludeOption = addOption({ { QStringLiteral("exclude") }, QStringLiteral("Path to an exclude list [file]"), QStringLiteral("file") });
    auto unsyncedfoldersOption = addOption(
        {{QStringLiteral("unsynced-folders")}, QStringLiteral("File containing the list of unsynced remote folders (selective sync)"), QStringLiteral("file")});

    auto nonInterActiveOption = addOption({ { QStringLiteral("non-interactive") }, QStringLiteral("Do not block execution with interaction") });
    auto maxRetriesOption = addOption({{QStringLiteral("max-sync-retries")}, QStringLiteral("Retries maximum n times (defaults to 3)"), QStringLiteral("n")});
    auto uploadLimitOption = addOption({{QStringLiteral("upload-limit")}, QStringLiteral("Limit the upload speed of files to n KB/s"), QStringLiteral("n")});
    auto downloadLimitption =
        addOption({{QStringLiteral("download-limit")}, QStringLiteral("Limit the download speed of files to n KB/s"), QStringLiteral("n")});
    auto syncHiddenFilesOption = addOption({ { QStringLiteral("sync-hidden-files") }, QStringLiteral("Enables synchronization of hidden files") });

    const auto testCrashReporter =
        addOption({{QStringLiteral("crash")}, QStringLiteral("Crash the client to test the crash reporter")}, QCommandLineOption::HiddenFromHelp);

    auto verbosityOption = addOption({{QStringLiteral("verbose")},
        QStringLiteral("Specify the [verbosity]\n0: no logging (default)\n"
                       "1: general logging\n"
                       "2: all previous and http logs\n"
                       "3: all previous and debug information"),
        QStringLiteral("verbosity"), QStringLiteral("0")});

    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument(
        QStringLiteral("server_url"), QStringLiteral("The URL to the OpenCloud installation on the server. This is usually the root path"));
    parser.addPositionalArgument(QStringLiteral("space_id"),
        QStringLiteral("The id, name or short id of the space to synchronize, if no [space_id] is provided or the [space_id] did not match any space, a list "
                       "of spaces is printed."),
        QStringLiteral("[space_id]"));
    parser.addPositionalArgument(QStringLiteral("source_dir"), QStringLiteral("The source dir"), QStringLiteral("[source_dir]"));

    parser.process(app_args);


    const int verbosity = parser.value(verbosityOption).toInt();
    if (verbosity >= 0 && verbosity <= 3) {
        logQuietMode = verbosity == 0;
        if (verbosity > 1) {
            Logger::instance()->addLogRule({QStringLiteral("sync.httplogger=true")});
        }
        if (verbosity > 2) {
            Logger::instance()->setLogDebug(true);
        }
    } else {
        qCritical() << u"Verbosity:" << verbosity << u"is not supported, valid verbosity level are 0, 1, 2";
        parser.showHelp(EXIT_FAILURE);
    }

    QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        options.server_url = QUrl::fromUserInput(args.takeFirst());
        if (!options.server_url.isValid()) {
            qCritical() << u"Invalid url: " << options.server_url.toString();
            parser.showHelp(EXIT_FAILURE);
        }
    } else {
        qCritical() << u"Please specify server_url";
        parser.showHelp(EXIT_FAILURE);
    }

    if (!args.isEmpty()) {
        options.space_id = args.takeFirst();
    } else {
        options.query = true;
    }

    if (!args.isEmpty()) {
        options.source_dir = [&parser, arg = args.takeFirst()] {
            const QFileInfo fi(arg);
            if (!fi.exists()) {
                qCritical() << u"Source dir" << fi.filePath() << u"does not exist.";
                parser.showHelp(EXIT_FAILURE);
            }
            QString sourceDir = fi.absoluteFilePath();
            if (!sourceDir.endsWith(QLatin1Char('/'))) {
                sourceDir.append(QLatin1Char('/'));
            }
            return sourceDir;
        }();
    }

    if (!args.isEmpty()) {
        qCritical() << u"Unhandled arguments" << args;
        parser.showHelp(EXIT_FAILURE);
    }

    if (parser.isSet(remoteFolder)) {
        options.remoteFolder = parser.value(remoteFolder);
    }

    if (parser.isSet(httpproxyOption)) {
        options.proxy = parser.value(httpproxyOption);
    }

    if (parser.isSet(trustOption)) {
        options.trustSSL = true;
    }
    if (parser.isSet(nonInterActiveOption)) {
        options.interactive = false;
    }
    if (parser.isSet(tokenOption)) {
        options.token = parser.value(tokenOption).toUtf8();
    } else if (options.token.isEmpty()) {
        qCritical() << u"Token not set";
        parser.showHelp(EXIT_FAILURE);
    }
    if (parser.isSet(userOption)) {
        options.username = parser.value(userOption).toUtf8();
    } else {
        qCritical() << u"Username not set";
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
    if (parser.isSet(testCrashReporter)) {
        // crash onc ethe main loop was started
        qCritical() << u"We'll soon crash on purpose";
        QTimer::singleShot(0, qApp, &Utility::crash);
    }
    return options;
}

int main(int argc, char **argv)
{
    auto platform = OCC::Platform::create(Platform::Type::Terminal);
    qSetMessagePattern(Logger::loggerPattern());
    qInstallMessageHandler(messageHandler);

    QCoreApplication app(argc, argv);

    platform->setApplication(&app);

    app.setApplicationVersion(Theme::instance()->versionSwitchOutput());

    SyncCTX ctx { parseOptions(app.arguments()) };

    // start the main loop before we ask for the username etc
    QTimer::singleShot(0, &app, [&] {
        ctx.account = Account::create(QUuid::createUuid());

        if (!ctx.account) {
            qCritical() << u"Could not initialize account!";
            exit(EXIT_FAILURE);
        }

        setupCredentials(ctx);

        // don't leak credentials more than needed
        ctx.options.server_url = ctx.options.server_url.adjusted(QUrl::RemoveUserInfo);
        ctx.account->setUrl(ctx.options.server_url);

        QObject::connect(ctx.account->credentials(), &AbstractCredentials::authenticationFailed, qApp,
            [] { qFatal() << u"Authentication failed please verify your credentials"; });

        auto *checkServerJob = CheckServerJobFactory(ctx.account->accessManager()).startJob(ctx.account->url(), qApp);

        QObject::connect(checkServerJob, &CoreJob::finished, qApp, [ctx, checkServerJob] {
            if (checkServerJob->success()) {
                // Perform a call to get the capabilities.
                auto *capabilitiesJob = new JsonApiJob(ctx.account, QStringLiteral("ocs/v1.php/cloud/capabilities"), {}, {}, nullptr);
                QObject::connect(capabilitiesJob, &JsonApiJob::finishedSignal, qApp, [capabilitiesJob, ctx] {
                    if (capabilitiesJob->reply()->error() != QNetworkReply::NoError || capabilitiesJob->httpStatusCode() != 200) {
                        qCritical() << u"Error connecting to server";
                        exit(EXIT_FAILURE);
                    }
                    auto caps = capabilitiesJob->data()
                                    .value(QStringLiteral("ocs"))
                                    .toObject()
                                    .value(QStringLiteral("data"))
                                    .toObject()
                                    .value(QStringLiteral("capabilities"))
                                    .toObject();
                    qDebug() << u"Server capabilities" << caps;
                    ctx.account->setCapabilities({ctx.account->url(), caps.toVariantMap()});

                    switch (ctx.account->serverSupportLevel()) {
                    case Account::ServerSupportLevel::Supported:
                        break;
                    case Account::ServerSupportLevel::Unknown:
                        qWarning() << u"Failed to detect server version";
                        break;
                    case Account::ServerSupportLevel::Unsupported:
                        qCritical() << u"Error unsupported server";
                        exit(EXIT_FAILURE);
                    }

                    if (ctx.options.query) {
                        QObject::connect(ctx.account->spacesManager(), &GraphApi::SpacesManager::ready, qApp, [ctx] {
                            printSpaces(ctx.account->spacesManager()->spaces());
                            qApp->quit();
                        });
                    } else {
                        QObject::connect(ctx.account->spacesManager(), &GraphApi::SpacesManager::ready, qApp, [ctx] {
                            GraphApi::Space *space = nullptr;
                            if (ctx.options.space_id.count(QLatin1Char('$')) == 1) {
                                space = ctx.account->spacesManager()->space(ctx.options.space_id);
                            } else {
                                const auto spaces = ctx.account->spacesManager()->spaces();
                                if (ctx.options.space_id.startsWith(QLatin1String("space:"))) {
                                    auto it = std::find_if(
                                        spaces.cbegin(), spaces.cend(), [&](const GraphApi::Space *s) { return hashSaceId(s->id()) == ctx.options.space_id; });
                                    if (it != spaces.cend()) {
                                        space = *it;
                                    }
                                } else {
                                    auto it = std::find_if(
                                        spaces.cbegin(), spaces.cend(), [&](const GraphApi::Space *s) { return s->displayName() == ctx.options.space_id; });
                                    if (it != spaces.cend()) {
                                        space = *it;
                                    }
                                }
                            }
                            if (!space) {
                                qCritical() << u"No spaces found matching:" << ctx.options.space_id;
                                printSpaces(ctx.account->spacesManager()->spaces());
                                qApp->exit(EXIT_FAILURE);
                                return;
                            }

                            // much lower age than the default since this utility is usually made to be run right after a change in the tests
                            SyncEngine::minimumFileAgeForUpload = std::chrono::seconds(0);
                            sync(ctx, space->webdavUrl());
                        });
                    }

                    // announce we are ready
                    Q_EMIT ctx.account->credentialsFetched();
                });
                capabilitiesJob->start();
            } else {
                if (checkServerJob->reply()->error() == QNetworkReply::OperationCanceledError) {
                    qFatal() << u"Looking up " << ctx.account->url().toString() << u" timed out.";
                } else {
                    qFatal() << u"Failed to resolve " << ctx.account->url().toString() << u" Error: " << checkServerJob->reply()->errorString();
                }
            }
        });
    });

    return app.exec();
}
