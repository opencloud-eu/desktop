/*
 *    This software is in the public domain, furnished "as is", without technical
 *       support, and with no warranty, express or implied, as to its usefulness for
 *          any purpose.
 *          */

#include <QtTest>

#include "networkjobs.h"

using namespace OCC;
using namespace Qt::StringLiterals;

class TestXmlParse : public QObject
{
    Q_OBJECT

private:
    bool _success;
    QStringList _subdirs;
    QStringList _items;
    QHash<QString, QMap<QString, QString>> _properties;

public Q_SLOTS:
    void slotDirectoryListingSubFolders(const QStringList &list)
    {
        qDebug() << u"subfolders: " << list;
        _subdirs.append(list);
    }

    void slotDirectoryListingIterated(const QString &item, const QMap<QString, QString> &properties)
    {
        qDebug() << u"     item: " << item;
        _items.append(item);
        _properties.insert(item, properties);
    }

    void slotFinishedSuccessfully() { _success = true; }

private Q_SLOTS:
    void init()
    {
        qDebug() << Q_FUNC_INFO;
        _success = false;
        _subdirs.clear();
        _items.clear();
        _properties.clear();
    }

    void cleanup() { }


    void testParser1()
    {
        const QByteArray testXml = "<?xml version='1.0' encoding='utf-8'?>"
                                   "<d:multistatus xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\" xmlns:oc=\"http://owncloud.org/ns\">"
                                   "<d:response>"
                                   "<d:href>/oc/remote.php/webdav/sharefolder/</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004213ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVCK</oc:permissions>"
                                   "<oc:size>121780</oc:size>"
                                   "<integrity-id xmlns=\"ocrclone\">0</integrity-id>"
                                   "<d:getetag>\"5527beb0400b0\"</d:getetag>"
                                   "<d:resourcetype>"
                                   "<d:collection/>"
                                   "</d:resourcetype>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<d:getcontentlength/>"
                                   "<oc:downloadURL/>"
                                   "<oc:dDC/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "<d:response>"
                                   "<d:href>/oc/remote.php/webdav/sharefolder/quitte.pdf</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004215ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVW</oc:permissions>"
                                   "<d:getetag>\"2fa2f0d9ed49ea0c3e409d49e652dea0\"</d:getetag>"
                                   "<d:resourcetype/>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "<d:getcontentlength>121780</d:getcontentlength>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:downloadURL/>"
                                   "<oc:dDC/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "</d:multistatus>";


        LsColXMLParser parser;

        connect(&parser, &LsColXMLParser::directoryListingSubfolders, this, &TestXmlParse::slotDirectoryListingSubFolders);
        connect(&parser, &LsColXMLParser::directoryListingIterated, this, &TestXmlParse::slotDirectoryListingIterated);
        connect(&parser, &LsColXMLParser::finishedWithoutError, this, &TestXmlParse::slotFinishedSuccessfully);

        QHash<QString, qint64> sizes;
        QSet<QString> vaults;
        QVERIFY(parser.parse(testXml, &sizes, &vaults, "/oc/remote.php/webdav/sharefolder"_L1));

        QVERIFY(_success);
        QCOMPARE(sizes.size(), 1); // Quota info in the XML
        QCOMPARE(vaults.size(), 1);

        QVERIFY(_items.contains("/oc/remote.php/webdav/sharefolder/quitte.pdf"_L1));
        QVERIFY(_items.contains("/oc/remote.php/webdav/sharefolder"_L1));
        QVERIFY(_items.size() == 2);

        QVERIFY(_subdirs.contains("/oc/remote.php/webdav/sharefolder/"_L1));
        QVERIFY(_subdirs.size() == 1);
    }

    void testParserBrokenXml()
    {
        const QByteArray testXml = "X<?xml version='1.0' encoding='utf-8'?>"
                                   "<d:multistatus xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\" xmlns:oc=\"http://owncloud.org/ns\">"
                                   "<d:response>"
                                   "<d:href>/oc/remote.php/webdav/sharefolder/</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004213ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVCK</oc:permissions>"
                                   "<oc:size>121780</oc:size>"
                                   "<integrity-id xmlns=\"ocrclone\">0</integrity-id>"
                                   "<d:getetag>\"5527beb0400b0\"</d:getetag>"
                                   "<d:resourcetype>"
                                   "<d:collection/>"
                                   "</d:resourcetype>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<d:getcontentlength/>"
                                   "<oc:downloadURL/>"
                                   "<oc:dDC/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "<d:response>"
                                   "<d:href>/oc/remote.php/webdav/sharefolder/quitte.pdf</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004215ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVW</oc:permissions>"
                                   "<d:getetag>\"2fa2f0d9ed49ea0c3e409d49e652dea0\"</d:getetag>"
                                   "<d:resourcetype/>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "<d:getcontentlength>121780</d:getcontentlength>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:downloadURL/>"
                                   "<oc:dDC/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "</d:multistatus>";


        LsColXMLParser parser;

        connect(&parser, &LsColXMLParser::directoryListingSubfolders, this, &TestXmlParse::slotDirectoryListingSubFolders);
        connect(&parser, &LsColXMLParser::directoryListingIterated, this, &TestXmlParse::slotDirectoryListingIterated);
        connect(&parser, &LsColXMLParser::finishedWithoutError, this, &TestXmlParse::slotFinishedSuccessfully);

        QHash<QString, qint64> sizes;
        QSet<QString> vaults;
        QVERIFY(false == parser.parse(testXml, &sizes, &vaults, "/oc/remote.php/webdav/sharefolder"_L1)); // verify false

        QVERIFY(!_success);
        QVERIFY(sizes.size() == 0); // No quota info in the XML
        QVERIFY(vaults.size() == 0);

        QVERIFY(_items.size() == 0); // FIXME: We should change the parser to not emit during parsing but at the end

        QVERIFY(_subdirs.size() == 0);
    }

    void testParserEmptyXmlNoDav()
    {
        const QByteArray testXml = "<html><body>I am under construction</body></html>";

        LsColXMLParser parser;

        connect(&parser, &LsColXMLParser::directoryListingSubfolders, this, &TestXmlParse::slotDirectoryListingSubFolders);
        connect(&parser, &LsColXMLParser::directoryListingIterated, this, &TestXmlParse::slotDirectoryListingIterated);
        connect(&parser, &LsColXMLParser::finishedWithoutError, this, &TestXmlParse::slotFinishedSuccessfully);

        QHash<QString, qint64> sizes;
        QSet<QString> vaults;
        QVERIFY(false == parser.parse(testXml, &sizes, &vaults, "/oc/remote.php/webdav/sharefolder"_L1)); // verify false

        QVERIFY(!_success);
        QVERIFY(sizes.size() == 0); // No quota info in the XML
        QVERIFY(vaults.size() == 0);

        QVERIFY(_items.size() == 0); // FIXME: We should change the parser to not emit during parsing but at the end
        QVERIFY(_subdirs.size() == 0);
    }

    void testParserEmptyXml()
    {
        const QByteArray testXml = "";

        LsColXMLParser parser;

        connect(&parser, &LsColXMLParser::directoryListingSubfolders, this, &TestXmlParse::slotDirectoryListingSubFolders);
        connect(&parser, &LsColXMLParser::directoryListingIterated, this, &TestXmlParse::slotDirectoryListingIterated);
        connect(&parser, &LsColXMLParser::finishedWithoutError, this, &TestXmlParse::slotFinishedSuccessfully);

        QHash<QString, qint64> sizes;
        QSet<QString> vaults;
        QVERIFY(false == parser.parse(testXml, &sizes, &vaults, "/oc/remote.php/webdav/sharefolder"_L1)); // verify false

        QVERIFY(!_success);
        QVERIFY(sizes.size() == 0); // No quota info in the XML
        QVERIFY(vaults.size() == 0);

        QVERIFY(_items.size() == 0); // FIXME: We should change the parser to not emit during parsing but at the end
        QVERIFY(_subdirs.size() == 0);
    }

    void testParserTruncatedXml()
    {
        const QByteArray testXml = "<?xml version='1.0' encoding='utf-8'?>"
                                   "<d:multistatus xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\" xmlns:oc=\"http://owncloud.org/ns\">"
                                   "<d:response>"
                                   "<d:href>/oc/remote.php/webdav/sharefolder/</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004213ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVCK</oc:permissions>"
                                   "<oc:size>121780</oc:size>"
                                   "<d:getetag>\"5527beb0400b0\"</d:getetag>"
                                   "<d:resourcetype>"
                                   "<d:collection/>"
                                   "</d:resourcetype>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"; // no proper end here


        LsColXMLParser parser;

        connect(&parser, &LsColXMLParser::directoryListingSubfolders, this, &TestXmlParse::slotDirectoryListingSubFolders);
        connect(&parser, &LsColXMLParser::directoryListingIterated, this, &TestXmlParse::slotDirectoryListingIterated);
        connect(&parser, &LsColXMLParser::finishedWithoutError, this, &TestXmlParse::slotFinishedSuccessfully);

        QHash<QString, qint64> sizes;
        QSet<QString> vaults;
        QVERIFY(!parser.parse(testXml, &sizes, &vaults, "/oc/remote.php/webdav/sharefolder"_L1));
        QVERIFY(!_success);
    }

    void testParserBogfusHref1()
    {
        const QByteArray testXml = "<?xml version='1.0' encoding='utf-8'?>"
                                   "<d:multistatus xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\" xmlns:oc=\"http://owncloud.org/ns\">"
                                   "<d:response>"
                                   "<d:href>http://127.0.0.1:81/oc/remote.php/webdav/sharefolder/</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004213ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVCK</oc:permissions>"
                                   "<oc:size>121780</oc:size>"
                                   "<d:getetag>\"5527beb0400b0\"</d:getetag>"
                                   "<d:resourcetype>"
                                   "<d:collection/>"
                                   "</d:resourcetype>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<d:getcontentlength/>"
                                   "<oc:downloadURL/>"
                                   "<oc:dDC/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "<d:response>"
                                   "<d:href>http://127.0.0.1:81/oc/remote.php/webdav/sharefolder/quitte.pdf</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004215ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVW</oc:permissions>"
                                   "<d:getetag>\"2fa2f0d9ed49ea0c3e409d49e652dea0\"</d:getetag>"
                                   "<d:resourcetype/>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "<d:getcontentlength>121780</d:getcontentlength>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:downloadURL/>"
                                   "<oc:dDC/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "</d:multistatus>";


        LsColXMLParser parser;

        connect(&parser, &LsColXMLParser::directoryListingSubfolders, this, &TestXmlParse::slotDirectoryListingSubFolders);
        connect(&parser, &LsColXMLParser::directoryListingIterated, this, &TestXmlParse::slotDirectoryListingIterated);
        connect(&parser, &LsColXMLParser::finishedWithoutError, this, &TestXmlParse::slotFinishedSuccessfully);

        QHash<QString, qint64> sizes;
        QSet<QString> vaults;
        QVERIFY(false == parser.parse(testXml, &sizes, &vaults, "/oc/remote.php/webdav/sharefolder"_L1));
        QVERIFY(!_success);
    }

    void testParserBogfusHref2()
    {
        const QByteArray testXml = "<?xml version='1.0' encoding='utf-8'?>"
                                   "<d:multistatus xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\" xmlns:oc=\"http://owncloud.org/ns\">"
                                   "<d:response>"
                                   "<d:href>/sharefolder</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004213ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVCK</oc:permissions>"
                                   "<oc:size>121780</oc:size>"
                                   "<d:getetag>\"5527beb0400b0\"</d:getetag>"
                                   "<d:resourcetype>"
                                   "<d:collection/>"
                                   "</d:resourcetype>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<d:getcontentlength/>"
                                   "<oc:downloadURL/>"
                                   "<oc:dDC/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "<d:response>"
                                   "<d:href>/sharefolder/quitte.pdf</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004215ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVW</oc:permissions>"
                                   "<d:getetag>\"2fa2f0d9ed49ea0c3e409d49e652dea0\"</d:getetag>"
                                   "<d:resourcetype/>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "<d:getcontentlength>121780</d:getcontentlength>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:downloadURL/>"
                                   "<oc:dDC/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "</d:multistatus>";


        LsColXMLParser parser;

        connect(&parser, &LsColXMLParser::directoryListingSubfolders, this, &TestXmlParse::slotDirectoryListingSubFolders);
        connect(&parser, &LsColXMLParser::directoryListingIterated, this, &TestXmlParse::slotDirectoryListingIterated);
        connect(&parser, &LsColXMLParser::finishedWithoutError, this, &TestXmlParse::slotFinishedSuccessfully);

        QHash<QString, qint64> sizes;
        QSet<QString> vaults;
        QVERIFY(false == parser.parse(testXml, &sizes, &vaults, "/oc/remote.php/webdav/sharefolder"_L1));
        QVERIFY(!_success);
    }

    void testHrefUrlEncoding()
    {
        const QByteArray testXml = "<?xml version='1.0' encoding='utf-8'?>"
                                   "<d:multistatus xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\" xmlns:oc=\"http://owncloud.org/ns\">"
                                   "<d:response>"
                                   "<d:href>/%C3%A4</d:href>" // a-umlaut utf8
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004213ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVCK</oc:permissions>"
                                   "<oc:size>121780</oc:size>"
                                   "<d:getetag>\"5527beb0400b0\"</d:getetag>"
                                   "<d:resourcetype>"
                                   "<d:collection/>"
                                   "</d:resourcetype>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<d:getcontentlength/>"
                                   "<oc:downloadURL/>"
                                   "<oc:dDC/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "<d:response>"
                                   "<d:href>/%C3%A4/%C3%A4.pdf</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:id>00004215ocobzus5kn6s</oc:id>"
                                   "<oc:permissions>RDNVW</oc:permissions>"
                                   "<d:getetag>\"2fa2f0d9ed49ea0c3e409d49e652dea0\"</d:getetag>"
                                   "<d:resourcetype/>"
                                   "<d:getlastmodified>Fri, 06 Feb 2015 13:49:55 GMT</d:getlastmodified>"
                                   "<d:getcontentlength>121780</d:getcontentlength>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:downloadURL/>"
                                   "<oc:dDC/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "</d:multistatus>";

        LsColXMLParser parser;

        connect(&parser, &LsColXMLParser::directoryListingSubfolders, this, &TestXmlParse::slotDirectoryListingSubFolders);
        connect(&parser, &LsColXMLParser::directoryListingIterated, this, &TestXmlParse::slotDirectoryListingIterated);
        connect(&parser, &LsColXMLParser::finishedWithoutError, this, &TestXmlParse::slotFinishedSuccessfully);

        QHash<QString, qint64> sizes;
        QSet<QString> vaults;
        QVERIFY(parser.parse(testXml, &sizes, &vaults, QString::fromUtf8("/ä")));
        QVERIFY(_success);

        QVERIFY(_items.contains(QString::fromUtf8("/ä/ä.pdf")));
        QVERIFY(_items.contains(QString::fromUtf8("/ä")));
        QVERIFY(_items.size() == 2);

        QVERIFY(_subdirs.contains(QString::fromUtf8("/ä")));
        QVERIFY(_subdirs.size() == 1);
    }

    void testParserNotFoundPropertiesDontLeak()
    {
        const QByteArray testXml = "<?xml version='1.0' encoding='utf-8'?>"
                                   "<d:multistatus xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\">"
                                   "<d:response>"
                                   "<d:href>/dav/a.txt</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<d:getetag>\"a\"</d:getetag>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:share-types/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "<d:response>"
                                   "<d:href>/dav/b.txt</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<d:getetag>\"b\"</d:getetag>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "<d:response>"
                                   "<d:href>/dav/c.txt</d:href>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<oc:share-types/>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 404 Not Found</d:status>"
                                   "</d:propstat>"
                                   "<d:propstat>"
                                   "<d:prop>"
                                   "<d:getetag>\"c\"</d:getetag>"
                                   "</d:prop>"
                                   "<d:status>HTTP/1.1 200 OK</d:status>"
                                   "</d:propstat>"
                                   "</d:response>"
                                   "</d:multistatus>";

        LsColXMLParser parser;

        connect(&parser, &LsColXMLParser::directoryListingIterated, this, &TestXmlParse::slotDirectoryListingIterated);
        connect(&parser, &LsColXMLParser::finishedWithoutError, this, &TestXmlParse::slotFinishedSuccessfully);

        QVERIFY(parser.parse(testXml, nullptr, nullptr, "/dav"_L1));
        QVERIFY(_success);
        QCOMPARE(_items.size(), 3);

        for (const auto &item : {"/dav/a.txt"_L1, "/dav/b.txt"_L1, "/dav/c.txt"_L1}) {
            QVERIFY(_properties.contains(item));
            QVERIFY2(!_properties[item].contains("share-types"_L1), qPrintable(item));
            QCOMPARE(_properties[item].keys(), QStringList{"getetag"_L1});
        }
    }
};

QTEST_GUILESS_MAIN(TestXmlParse)

#include "testxmlparse.moc"
