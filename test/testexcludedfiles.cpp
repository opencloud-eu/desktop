/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include "libsync/configfile.h"

#include <QtTest>

#include "libsync/csync_exclude.h"
#include "testutils.h"

using namespace OCC;
using namespace Qt::StringLiterals;

// The tests were converted from the old CMocka framework, that's why there is a global
static QScopedPointer<ExcludedFiles> excludedFiles;

static void setup() {
    excludedFiles.reset(new ExcludedFiles);
    excludedFiles->setWildcardsMatchSlash(false);
}

static void setup_init() {
    setup();

    excludedFiles->addExcludeFilePath(ConfigFile::defaultExcludeFile());
    QVERIFY(excludedFiles->reloadExcludeFiles());

    /* and add some unicode stuff */
    excludedFiles->addManualExclude(u"*.💩"_s); // is this source file utf8 encoded?
    excludedFiles->addManualExclude(u"пятницы.*"_s);
    excludedFiles->addManualExclude(u"*/*.out"_s);
    excludedFiles->addManualExclude(u"latex*/*.run.xml"_s);
    excludedFiles->addManualExclude(u"latex/*/*.tex.tmp"_s);

    QVERIFY(excludedFiles->reloadExcludeFiles());
}

class TestExcludedFiles: public QObject
{
    Q_OBJECT

    static auto check_file_full(const QString &path) { return excludedFiles->fullPatternMatch(path, ItemTypeFile); }

    static auto check_dir_full(const QString &path) { return excludedFiles->fullPatternMatch(path, ItemTypeDirectory); }

    static auto check_file_traversal(const QString &path) { return excludedFiles->traversalPatternMatch(path, ItemTypeFile); }

    static auto check_dir_traversal(const QString &path) { return excludedFiles->traversalPatternMatch(path, ItemTypeDirectory); }

private Q_SLOTS:
    void testFun()
    {
        ExcludedFiles excluded;
        bool excludeHidden = true;
        bool keepHidden = false;

        auto check_isExcluded = [&](const QString &a, bool keepHidden, bool create = true) {
            auto tmp = OCC::TestUtils::createTempDir();
            Q_ASSERT(tmp.isValid());

            auto createTree = [&](const QString &path) {
                auto parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
                const auto fileName = parts.back();
                parts.pop_back(); // remove file name
                QString workPath = tmp.path() + QLatin1Char('/');
                for (const auto &p : std::as_const(parts)) {
                    QDir dir(workPath);
                    dir.mkdir(p);
                    workPath += p + QLatin1Char('/');
                }
                workPath += fileName;
                QFile file(workPath);
                QVERIFY(file.open(QFile::WriteOnly));
                file.write("OpenCloud");
                file.close();
            };
            if (create) {
                createTree(a);
            }
            return excluded.isExcluded(QString{tmp.path() + a}, QString{tmp.path() + "/a"_L1}, keepHidden);
        };

        QVERIFY(!check_isExcluded("/a/b"_L1, keepHidden));
        QVERIFY(!check_isExcluded("/a/b~"_L1, keepHidden));
        QVERIFY(!check_isExcluded("/a/.b"_L1, keepHidden));
        QVERIFY(check_isExcluded("/a/.b"_L1, excludeHidden));

        excluded.addExcludeFilePath(ConfigFile::defaultExcludeFile());
        excluded.reloadExcludeFiles();

        QVERIFY(!check_isExcluded("/a/b"_L1, keepHidden));
        QVERIFY(check_isExcluded("/a/b~"_L1, keepHidden));
        QVERIFY(!check_isExcluded("/a/.b"_L1, keepHidden));
        QVERIFY(check_isExcluded("/a/.Trashes"_L1, keepHidden));

        QVERIFY(check_isExcluded("/a/.Trash"_L1, keepHidden));
        QVERIFY(check_isExcluded("/a/.Trash-100"_L1, keepHidden));
        // what ever .Trash- is, it doesn't follow https://specifications.freedesktop.org/trash/1.0/
        QVERIFY(!check_isExcluded("/a/.Trash-"_L1, keepHidden));

        QVERIFY(check_isExcluded("/a/foo_conflict-bar"_L1, keepHidden));
        QVERIFY(check_isExcluded("/a/foo (conflicted copy bar)"_L1, keepHidden));
        QVERIFY(check_isExcluded("/a/.b"_L1, excludeHidden));

        // test non existing folder
        QVERIFY(check_isExcluded("/a/.b"_L1, excludeHidden, false));
    }

    void check_csync_exclude_add()
    {
        setup();
        excludedFiles->addManualExclude("/tmp/check_csync1/*"_L1);
        QCOMPARE(check_file_full("/tmp/check_csync1/foo"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full("/tmp/check_csync2/foo"_L1), CSYNC_NOT_EXCLUDED);
        QVERIFY(excludedFiles->_allExcludes.contains("/tmp/check_csync1/*"_L1));

        QVERIFY(excludedFiles->_fullRegexFile.pattern().contains("csync1"_L1));
        QVERIFY(excludedFiles->_fullTraversalRegexFile.pattern().contains("csync1"_L1));
        QVERIFY(!excludedFiles->_bnameTraversalRegexFile.pattern().contains("csync1"_L1));

        excludedFiles->addManualExclude("foo"_L1);
        QVERIFY(excludedFiles->_bnameTraversalRegexFile.pattern().contains("foo"_L1));
        QVERIFY(excludedFiles->_fullRegexFile.pattern().contains("foo"_L1));
        QVERIFY(!excludedFiles->_fullTraversalRegexFile.pattern().contains("foo"_L1));
    }

    void check_csync_excluded()
    {
        setup_init();
        QCOMPARE(check_file_full(QString()), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("/"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("A"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("krawel_krawel"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full(".kde/share/config/kwin.eventsrc"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full(".directory/cache-maximegalon/cache1.txt"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_full("mozilla/.directory"_L1), CSYNC_FILE_EXCLUDE_LIST);

        /*
        * Test for patterns in subdirs. '.beagle' is defined as a pattern and has
        * to be found in top dir as well as in directories underneath.
        */
        QCOMPARE(check_dir_full(".apdisk"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_full("foo/.apdisk"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_full("foo/bar/.apdisk"_L1), CSYNC_FILE_EXCLUDE_LIST);

        QCOMPARE(check_file_full(".java"_L1), CSYNC_NOT_EXCLUDED);

        /* Files in the ignored dir .java will also be ignored. */
        QCOMPARE(check_file_full(".apdisk/totally_amazing.jar"_L1), CSYNC_FILE_EXCLUDE_LIST);

        /* and also in subdirs */
        QCOMPARE(check_file_full("projects/.apdisk/totally_amazing.jar"_L1), CSYNC_FILE_EXCLUDE_LIST);

        /* sync-journal is ignored in general silently. */
        QCOMPARE(check_file_full(".sync_journal.db"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);
        QCOMPARE(check_file_full(".sync_journal.db.ctmp"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);
        QCOMPARE(check_file_full("subdir/.sync_journal.db"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);

        /* pattern ]*.directory - ignore and remove */
        QCOMPARE(check_file_full("my.~directory"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);
        QCOMPARE(check_file_full("/a_folder/my.~directory"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);

        /* Not excluded because the pattern .netscape/cache requires directory. */
        QCOMPARE(check_file_full(".netscape/cache"_L1), CSYNC_NOT_EXCLUDED);

        /* Not excluded  */
        QCOMPARE(check_file_full(u"unicode/中文.hé"_s), CSYNC_NOT_EXCLUDED);
        /* excluded  */
        QCOMPARE(check_file_full(u"unicode/пятницы.txt"_s), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full(u"unicode/中文.💩"_s), CSYNC_FILE_EXCLUDE_LIST);

        /* path wildcards */
        QCOMPARE(check_file_full("foobar/my_manuscript.out"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full("latex_tmp/my_manuscript.run.xml"_L1), CSYNC_FILE_EXCLUDE_LIST);

        QCOMPARE(check_file_full("word_tmp/my_manuscript.run.xml"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_file_full("latex/my_manuscript.tex.tmp"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_file_full("latex/songbook/my_manuscript.tex.tmp"_L1), CSYNC_FILE_EXCLUDE_LIST);

#ifdef _WIN32
        QCOMPARE(check_file_full("file_trailing_space "_L1), CSYNC_FILE_EXCLUDE_TRAILING_SPACE);
        QCOMPARE(check_file_full("file_trailing_dot."_L1), CSYNC_FILE_EXCLUDE_INVALID_CHAR);
        QCOMPARE(check_file_full("AUX"_L1), CSYNC_FILE_EXCLUDE_INVALID_CHAR);
        QCOMPARE(check_file_full("file_invalid_char<"_L1), CSYNC_FILE_EXCLUDE_INVALID_CHAR);
        QCOMPARE(check_file_full("file_invalid_char\n"_L1), CSYNC_FILE_EXCLUDE_INVALID_CHAR);
#endif

        /* ? character */
        excludedFiles->addManualExclude("bond00?"_L1);
        excludedFiles->reloadExcludeFiles();
        QCOMPARE(check_file_full("bond00"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("bond007"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full("bond0071"_L1), CSYNC_NOT_EXCLUDED);

        /* brackets */
        excludedFiles->addManualExclude("a [bc] d"_L1);
        excludedFiles->reloadExcludeFiles();
        QCOMPARE(check_file_full("a d d"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("a  d"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("a b d"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full("a c d"_L1), CSYNC_FILE_EXCLUDE_LIST);

#ifndef Q_OS_WIN   // Because of CSYNC_FILE_EXCLUDE_INVALID_CHAR on windows
        /* escapes */
        excludedFiles->addManualExclude("a \\*"_L1);
        excludedFiles->addManualExclude("b \\?"_L1);
        excludedFiles->addManualExclude("c \\[d]"_L1);
        excludedFiles->reloadExcludeFiles();
        QCOMPARE(check_file_full("a \\*"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("a bc"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("a *"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full("b \\?"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("b f"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("b ?"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full("c \\[d]"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("c d"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_full("c [d]"_L1), CSYNC_FILE_EXCLUDE_LIST);
#endif
    }

    void check_csync_excluded_traversal()
    {
        setup_init();
        QCOMPARE(check_file_traversal(QString()), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("/"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_file_traversal("A"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_file_traversal("krawel_krawel"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal(".kde/share/config/kwin.eventsrc"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_dir_traversal("mozilla/.directory"_L1), CSYNC_FILE_EXCLUDE_LIST);

        /*
        * Test for patterns in subdirs. '.beagle' is defined as a pattern and has
        * to be found in top dir as well as in directories underneath.
        */
        QCOMPARE(check_dir_traversal(".apdisk"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_traversal("foo/.apdisk"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_traversal("foo/bar/.apdisk"_L1), CSYNC_FILE_EXCLUDE_LIST);

        QCOMPARE(check_file_traversal(".java"_L1), CSYNC_NOT_EXCLUDED);

        /* csync-journal is ignored in general silently. */
        QCOMPARE(check_file_traversal(".sync_journal.db"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);
        QCOMPARE(check_file_traversal(".sync_journal.db.ctmp"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);
        QCOMPARE(check_file_traversal("subdir/.sync_journal.db"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);
        QCOMPARE(check_file_traversal("/two/subdir/.sync_journal.db"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);

        /* Other builtin excludes */
        QCOMPARE(check_file_traversal("foo/Desktop.ini"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);
        QCOMPARE(check_file_traversal("Desktop.ini"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);

        /* pattern ]*.directory - ignore and remove */
        QCOMPARE(check_file_traversal("my.~directory"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);
        QCOMPARE(check_file_traversal("/a_folder/my.~directory"_L1), CSYNC_FILE_SILENTLY_EXCLUDED);

        /* Not excluded because the pattern .netscape/cache requires directory. */
        QCOMPARE(check_file_traversal(".netscape/cache"_L1), CSYNC_NOT_EXCLUDED);

        /* Not excluded  */
        QCOMPARE(check_file_traversal(u"unicode/中文.hé"_s), CSYNC_NOT_EXCLUDED);
        /* excluded  */
        QCOMPARE(check_file_traversal(u"unicode/пятницы.txt"_s), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal(u"unicode/中文.💩"_s), CSYNC_FILE_EXCLUDE_LIST);

        /* path wildcards */
        QCOMPARE(check_file_traversal("foobar/my_manuscript.out"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("latex_tmp/my_manuscript.run.xml"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("word_tmp/my_manuscript.run.xml"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("latex/my_manuscript.tex.tmp"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("latex/songbook/my_manuscript.tex.tmp"_L1), CSYNC_FILE_EXCLUDE_LIST);

#ifdef _WIN32
        QCOMPARE(check_file_traversal("file_trailing_space "_L1), CSYNC_FILE_EXCLUDE_TRAILING_SPACE);
        QCOMPARE(check_file_traversal("file_trailing_dot."_L1), CSYNC_FILE_EXCLUDE_INVALID_CHAR);
        QCOMPARE(check_file_traversal("AUX"_L1), CSYNC_FILE_EXCLUDE_INVALID_CHAR);
        QCOMPARE(check_file_traversal("file_invalid_char<"_L1), CSYNC_FILE_EXCLUDE_INVALID_CHAR);
        QCOMPARE(check_file_traversal("file_invalid_char\n"_L1), CSYNC_FILE_EXCLUDE_INVALID_CHAR);
#endif


        /* From here the actual traversal tests */

        excludedFiles->addManualExclude("/exclude"_L1);
        excludedFiles->reloadExcludeFiles();

        /* Check toplevel dir, the pattern only works for toplevel dir. */
        QCOMPARE(check_dir_traversal("/exclude"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_traversal("/foo/exclude"_L1), CSYNC_NOT_EXCLUDED);

        /* check for a file called exclude. Must still work */
        QCOMPARE(check_file_traversal("/exclude"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("/foo/exclude"_L1), CSYNC_NOT_EXCLUDED);

        /* Add an exclude for directories only: excl/ */
        excludedFiles->addManualExclude("excl/"_L1);
        excludedFiles->reloadExcludeFiles();
        QCOMPARE(check_dir_traversal("/excl"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_traversal("meep/excl"_L1), CSYNC_FILE_EXCLUDE_LIST);

        // because leading dirs aren't checked!
        QCOMPARE(check_file_traversal("meep/excl/file"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("/excl"_L1), CSYNC_NOT_EXCLUDED);

        excludedFiles->addManualExclude("/excludepath/withsubdir"_L1);
        excludedFiles->reloadExcludeFiles();

        QCOMPARE(check_dir_traversal("/excludepath/withsubdir"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("/excludepath/withsubdir"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_traversal("/excludepath/withsubdir2"_L1), CSYNC_NOT_EXCLUDED);

        // because leading dirs aren't checked!
        QCOMPARE(check_dir_traversal("/excludepath/withsubdir/foo"_L1), CSYNC_NOT_EXCLUDED);

        /* Check ending of pattern */
        QCOMPARE(check_file_traversal("/exclude"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("/excludeX"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("exclude"_L1), CSYNC_NOT_EXCLUDED);

        excludedFiles->addManualExclude("exclude"_L1);
        excludedFiles->reloadExcludeFiles();
        QCOMPARE(check_file_traversal("exclude"_L1), CSYNC_FILE_EXCLUDE_LIST);

        /* ? character */
        excludedFiles->addManualExclude("bond00?"_L1);
        excludedFiles->reloadExcludeFiles();
        QCOMPARE(check_file_traversal("bond00"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("bond007"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("bond0071"_L1), CSYNC_NOT_EXCLUDED);

        /* brackets */
        excludedFiles->addManualExclude("a [bc] d"_L1);
        excludedFiles->reloadExcludeFiles();
        QCOMPARE(check_file_traversal("a d d"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("a  d"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("a b d"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("a c d"_L1), CSYNC_FILE_EXCLUDE_LIST);

#ifndef Q_OS_WIN   // Because of CSYNC_FILE_EXCLUDE_INVALID_CHAR on windows
        /* escapes */
        excludedFiles->addManualExclude("a \\*"_L1);
        excludedFiles->addManualExclude("b \\?"_L1);
        excludedFiles->addManualExclude("c \\[d]"_L1);
        excludedFiles->reloadExcludeFiles();
        QCOMPARE(check_file_traversal("a \\*"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("a bc"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("a *"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("b \\?"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("b f"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("b ?"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("c \\[d]"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("c d"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("c [d]"_L1), CSYNC_FILE_EXCLUDE_LIST);
#endif
    }

    void check_csync_dir_only()
    {
        setup();
        excludedFiles->addManualExclude("filedir"_L1);
        excludedFiles->addManualExclude("dir/"_L1);

        QCOMPARE(check_file_traversal("other"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("filedir"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("dir"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("s/other"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_file_traversal("s/filedir"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("s/dir"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_dir_traversal("other"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_dir_traversal("filedir"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_traversal("dir"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_traversal("s/other"_L1), CSYNC_NOT_EXCLUDED);
        QCOMPARE(check_dir_traversal("s/filedir"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_traversal("s/dir"_L1), CSYNC_FILE_EXCLUDE_LIST);

        QCOMPARE(check_dir_full("filedir/foo"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full("filedir/foo"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_full("dir/foo"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full("dir/foo"_L1), CSYNC_FILE_EXCLUDE_LIST);
    }

    void check_csync_pathes()
    {
        setup_init();
        excludedFiles->addManualExclude("/exclude"_L1);
        excludedFiles->reloadExcludeFiles();

        /* Check toplevel dir, the pattern only works for toplevel dir. */
        QCOMPARE(check_dir_full("/exclude"_L1), CSYNC_FILE_EXCLUDE_LIST);

        QCOMPARE(check_dir_full("/foo/exclude"_L1), CSYNC_NOT_EXCLUDED);

        /* check for a file called exclude. Must still work */
        QCOMPARE(check_file_full("/exclude"_L1), CSYNC_FILE_EXCLUDE_LIST);

        QCOMPARE(check_file_full("/foo/exclude"_L1), CSYNC_NOT_EXCLUDED);

        /* Add an exclude for directories only: excl/ */
        excludedFiles->addManualExclude("excl/"_L1);
        excludedFiles->reloadExcludeFiles();
        QCOMPARE(check_dir_full("/excl"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_dir_full("meep/excl"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full("meep/excl/file"_L1), CSYNC_FILE_EXCLUDE_LIST);

        QCOMPARE(check_file_full("/excl"_L1), CSYNC_NOT_EXCLUDED);

        excludedFiles->addManualExclude("/excludepath/withsubdir"_L1);
        excludedFiles->reloadExcludeFiles();

        QCOMPARE(check_dir_full("/excludepath/withsubdir"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_full("/excludepath/withsubdir"_L1), CSYNC_FILE_EXCLUDE_LIST);

        QCOMPARE(check_dir_full("/excludepath/withsubdir2"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_dir_full("/excludepath/withsubdir/foo"_L1), CSYNC_FILE_EXCLUDE_LIST);
    }

    void check_csync_wildcards()
    {
        setup();
        excludedFiles->addManualExclude("a/foo*bar"_L1);
        excludedFiles->addManualExclude("b/foo*bar*"_L1);
        excludedFiles->addManualExclude("c/foo?bar"_L1);
        excludedFiles->addManualExclude("d/foo?bar*"_L1);
        excludedFiles->addManualExclude("e/foo?bar?"_L1);
        excludedFiles->addManualExclude("g/bar*"_L1);
        excludedFiles->addManualExclude("h/bar?"_L1);

        excludedFiles->setWildcardsMatchSlash(false);

        QCOMPARE(check_file_traversal("a/fooXYZbar"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("a/fooX/Zbar"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_file_traversal("b/fooXYZbarABC"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("b/fooX/ZbarABC"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_file_traversal("c/fooXbar"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("c/foo/bar"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_file_traversal("d/fooXbarABC"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("d/foo/barABC"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_file_traversal("e/fooXbarA"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("e/foo/barA"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_file_traversal("g/barABC"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("g/XbarABC"_L1), CSYNC_NOT_EXCLUDED);

        QCOMPARE(check_file_traversal("h/barZ"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("h/XbarZ"_L1), CSYNC_NOT_EXCLUDED);

        excludedFiles->setWildcardsMatchSlash(true);

        QCOMPARE(check_file_traversal("a/fooX/Zbar"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("b/fooX/ZbarABC"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("c/foo/bar"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("d/foo/barABC"_L1), CSYNC_FILE_EXCLUDE_LIST);
        QCOMPARE(check_file_traversal("e/foo/barA"_L1), CSYNC_FILE_EXCLUDE_LIST);
    }

    void check_csync_regex_translation()
    {
        setup();
        auto translate = [](const QString &pattern) { return ExcludedFiles::convertToRegexpSyntax(pattern, false).toUtf8(); };

        QCOMPARE(translate(QString()), "");
        QCOMPARE(translate("abc"_L1), "abc");
        QCOMPARE(translate("a*c"_L1), "a[^/]*c");
        QCOMPARE(translate("a?c"_L1), "a[^/]c");
        QCOMPARE(translate("a[xyz]c"_L1), "a[xyz]c");
        QCOMPARE(translate("a[xyzc"_L1), "a\\[xyzc");
        QCOMPARE(translate("a[!xyz]c"_L1), "a[^xyz]c");
        QCOMPARE(translate("a\\*b\\?c\\[d\\\\e"_L1), "a\\*b\\?c\\[d\\\\e");
        QCOMPARE(translate("a.c"_L1), "a\\.c");
        QCOMPARE(translate(u"?𠜎?"_s), "[^/]\\𠜎[^/]"); // 𠜎 is 4-byte utf8
    }

    void check_csync_bname_trigger()
    {
        setup();
        bool wildcardsMatchSlash = false;
        auto translate = [&wildcardsMatchSlash](const QString &pattern) { return ExcludedFiles::extractBnameTrigger(pattern, wildcardsMatchSlash).toUtf8(); };

        QCOMPARE(translate(QString()), "");
        QCOMPARE(translate("a/b/"_L1), "");
        QCOMPARE(translate("a/b/c"_L1), "c");
        QCOMPARE(translate("c"_L1), "c");
        QCOMPARE(translate("a/foo*"_L1), "foo*");
        QCOMPARE(translate("a/abc*foo*"_L1), "abc*foo*");

        wildcardsMatchSlash = true;

        QCOMPARE(translate(QString()), "");
        QCOMPARE(translate("a/b/"_L1), "");
        QCOMPARE(translate("a/b/c"_L1), "c");
        QCOMPARE(translate("c"_L1), "c");
        QCOMPARE(translate("*"_L1), "*");
        QCOMPARE(translate("a/foo*"_L1), "foo*");
        QCOMPARE(translate("a/abc?foo*"_L1), "*foo*");
        QCOMPARE(translate("a/abc*foo*"_L1), "*foo*");
        QCOMPARE(translate("a/abc?foo?"_L1), "*foo?");
        QCOMPARE(translate("a/abc*foo?*"_L1), "*foo?*");
        QCOMPARE(translate("a/abc*/foo*"_L1), "foo*");
    }

    void check_csync_is_windows_reserved_word()
    {
        auto csync_is_windows_reserved_word = [](const char *fn) {
            QString s = QString::fromLatin1(fn);
            extern bool csync_is_windows_reserved_word(QStringView filename);
            return csync_is_windows_reserved_word(s);
        };

        QVERIFY(csync_is_windows_reserved_word("CON"));
        QVERIFY(csync_is_windows_reserved_word("con"));
        QVERIFY(csync_is_windows_reserved_word("CON."));
        QVERIFY(csync_is_windows_reserved_word("con."));
        QVERIFY(csync_is_windows_reserved_word("CON.ference"));
        QVERIFY(!csync_is_windows_reserved_word("CONference"));
        QVERIFY(!csync_is_windows_reserved_word("conference"));
        QVERIFY(!csync_is_windows_reserved_word("conf.erence"));
        QVERIFY(!csync_is_windows_reserved_word("co"));

        QVERIFY(csync_is_windows_reserved_word("COM2"));
        QVERIFY(csync_is_windows_reserved_word("com2"));
        QVERIFY(csync_is_windows_reserved_word("COM2."));
        QVERIFY(csync_is_windows_reserved_word("com2."));
        QVERIFY(csync_is_windows_reserved_word("COM2.ference"));
        QVERIFY(!csync_is_windows_reserved_word("COM2ference"));
        QVERIFY(!csync_is_windows_reserved_word("com2ference"));
        QVERIFY(!csync_is_windows_reserved_word("com2f.erence"));
        QVERIFY(!csync_is_windows_reserved_word("com"));

        QVERIFY(csync_is_windows_reserved_word("CLOCK$"));
        QVERIFY(csync_is_windows_reserved_word("$Recycle.Bin"));
        QVERIFY(csync_is_windows_reserved_word("ClocK$"));
        QVERIFY(csync_is_windows_reserved_word("$recycle.bin"));

        QVERIFY(csync_is_windows_reserved_word("A:"));
        QVERIFY(csync_is_windows_reserved_word("a:"));
        QVERIFY(csync_is_windows_reserved_word("z:"));
        QVERIFY(csync_is_windows_reserved_word("Z:"));
        QVERIFY(csync_is_windows_reserved_word("M:"));
        QVERIFY(csync_is_windows_reserved_word("m:"));
    }

    /* QT_ENABLE_REGEXP_JIT=0 to get slower results :-) */
    void check_csync_excluded_performance1()
    {
        setup_init();
        const int N = 1000;
        int totalRc = 0;

        QBENCHMARK {

            for (int i = 0; i < N; ++i) {
                totalRc += check_dir_full("/this/is/quite/a/long/path/with/many/components"_L1);
                totalRc += check_file_full("/1/2/3/4/5/6/7/8/9/10/11/12/13/14/15/16/17/18/19/20/21/22/23/24/25/26/27/29"_L1);
            }
            QCOMPARE(totalRc, 0); // mainly to avoid optimization
        }
    }

    void check_csync_excluded_performance2()
    {
        const int N = 1000;
        int totalRc = 0;

        QBENCHMARK {
            for (int i = 0; i < N; ++i) {
                totalRc += check_dir_traversal("/this/is/quite/a/long/path/with/many/components"_L1);
                totalRc += check_file_traversal("/1/2/3/4/5/6/7/8/9/10/11/12/13/14/15/16/17/18/19/20/21/22/23/24/25/26/27/29"_L1);
            }
            QCOMPARE(totalRc, 0); // mainly to avoid optimization
        }
    }

    void check_csync_exclude_expand_escapes()
    {
        extern void csync_exclude_expand_escapes(QByteArray &input);

        QByteArray line = "keep \\' \\\" \\? \\\\ \\a \\b \\f \\n \\r \\t \\v \\z \\#";
        csync_exclude_expand_escapes(line);
        QVERIFY(0 == strcmp(line.constData(), "keep ' \" ? \\\\ \a \b \f \n \r \t \v \\z #"));

        line = "";
        csync_exclude_expand_escapes(line);
        QVERIFY(0 == strcmp(line.constData(), ""));

        line = "\\";
        csync_exclude_expand_escapes(line);
        QVERIFY(0 == strcmp(line.constData(), "\\"));
    }

    void check_version_directive()
    {
        ExcludedFiles excludes;
        excludes.setClientVersion(QVersionNumber(2, 5, 0));

        std::vector<std::pair<const char *, bool>> tests = {
            { "#!version == 2.5.0", true },
            { "#!version == 2.6.0", false },
            { "#!version < 2.6.0", true },
            { "#!version <= 2.6.0", true },
            { "#!version > 2.6.0", false },
            { "#!version >= 2.6.0", false },
            { "#!version < 2.4.0", false },
            { "#!version <= 2.4.0", false },
            { "#!version > 2.4.0", true },
            { "#!version >= 2.4.0", true },
            { "#!version < 2.5.0", false },
            { "#!version <= 2.5.0", true },
            { "#!version > 2.5.0", false },
            { "#!version >= 2.5.0", true },
        };
        for (auto test : tests) {
            QVERIFY(excludes.versionDirectiveKeepNextLine(test.first) == test.second);
        }
    }

};

QTEST_GUILESS_MAIN(TestExcludedFiles)
#include "testexcludedfiles.moc"
