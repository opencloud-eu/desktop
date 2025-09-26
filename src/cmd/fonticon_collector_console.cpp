// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 OpenCloud FontIcon Collector Tool - Console Version

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QCommandLineParser>

struct FontIconUsage {
    QString filePath;
    int lineNumber;
    QString sourceContext;
    QChar glyph;
    QString family;
    QString size;
    QString fullConstructorCall;
};

class FontIconCollector
{
public:
    void scanForFontIconUsages()
    {
        QString basePath = QDir::current().absolutePath();
        
        // Look for src directory relative to current directory
        QDir srcDir(basePath + "/src");
        if (!srcDir.exists()) {
            // Try parent directories
            QDir parentDir = QDir::current();
            while (parentDir.cdUp()) {
                if (QDir(parentDir.path() + "/src").exists()) {
                    srcDir = QDir(parentDir.path() + "/src");
                    break;
                }
            }
        }
        
        if (!srcDir.exists()) {
            qWarning() << "Could not find src directory";
            return;
        }
        
        qDebug() << "Scanning directory:" << srcDir.absolutePath();
        
        // Recursively scan for C++ files
        QDirIterator it(srcDir.absolutePath(), QStringList() << "*.cpp" << "*.h", QDir::Files, QDirIterator::Subdirectories);
        
        while (it.hasNext()) {
            QString filePath = it.next();
            scanFile(filePath);
        }
        
        qDebug() << "Found" << m_usages.size() << "FontIcon usages";
        
        // Print results
        printResults();
    }

private:
    void scanFile(const QString &filePath)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return;
        }
        
        QTextStream in(&file);
        QString content = in.readAll();
        QStringList lines = content.split('\n');
        
        // Regular expressions to match FontIcon constructor calls
        QRegularExpression fontIconRegex(R"(FontIcon\s*\([^)]*\))");
        
        for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
            const QString &line = lines[lineNum];
            
            auto fontIconMatches = fontIconRegex.globalMatch(line);
            while (fontIconMatches.hasNext()) {
                auto fontIconMatch = fontIconMatches.next();
                
                FontIconUsage usage;
                usage.filePath = filePath;
                usage.lineNumber = lineNum + 1;
                usage.fullConstructorCall = fontIconMatch.captured(0);
                
                // Extract glyph - look for various patterns
                QString constructorContent = fontIconMatch.captured(0);
                
                // Pattern 1: u'X' (single character)
                QRegularExpression glyphRegex(R"(u'(.)')");
                auto glyphMatch = glyphRegex.match(constructorContent);
                if (glyphMatch.hasMatch()) {
                    usage.glyph = glyphMatch.captured(1)[0];
                } else {
                    // Pattern 2: u'\uXXXX' (hex unicode)
                    QRegularExpression hexGlyphRegex(R"(u'\\u([0-9a-fA-F]{4})')");
                    auto hexMatch = hexGlyphRegex.match(constructorContent);
                    if (hexMatch.hasMatch()) {
                        bool ok;
                        uint unicode = hexMatch.captured(1).toUInt(&ok, 16);
                        if (ok) {
                            usage.glyph = QChar(unicode);
                        } else {
                            usage.glyph = QChar('?');
                        }
                    } else {
                        // Pattern 3: Direct unicode character
                        QRegularExpression directCharRegex(R"('[^']*')");
                        auto directMatch = directCharRegex.match(constructorContent);
                        if (directMatch.hasMatch()) {
                            QString captured = directMatch.captured(0);
                            if (captured.length() >= 3) { // At least '?'
                                usage.glyph = captured[1]; // Character between quotes
                            } else {
                                usage.glyph = QChar('?');
                            }
                        } else {
                            usage.glyph = QChar('?'); // Unknown glyph
                        }
                    }
                }
                
                // Extract family (default to FontAwesome)
                QRegularExpression familyRegex(R"(FontIcon::FontFamily::(\w+))");
                auto familyMatch = familyRegex.match(constructorContent);
                if (familyMatch.hasMatch()) {
                    usage.family = familyMatch.captured(1);
                } else {
                    usage.family = "FontAwesome";
                }
                
                // Extract size (default to Normal)
                QRegularExpression sizeRegex(R"(FontIcon::Size::(\w+))");
                auto sizeMatch = sizeRegex.match(constructorContent);
                if (sizeMatch.hasMatch()) {
                    usage.size = sizeMatch.captured(1);
                } else {
                    usage.size = "Normal";
                }
                
                // Get context line
                usage.sourceContext = line.trimmed();
                
                m_usages.append(usage);
            }
        }
    }
    
    void printResults()
    {
        QTextStream out(stdout);
        
        out << "\n==== FontIcon Usage Report ====\n";
        out << "Found " << m_usages.size() << " FontIcon usages in the codebase:\n\n";
        
        for (int i = 0; i < m_usages.size(); ++i) {
            const auto &usage = m_usages[i];
            
            out << "[" << (i + 1) << "] " << QFileInfo(usage.filePath).fileName() 
                << ":" << usage.lineNumber << "\n";
            out << "  Glyph: '" << usage.glyph << "' (Unicode: U+" 
                << QString::number(usage.glyph.unicode(), 16).toUpper().rightJustified(4, '0') << ")\n";
            out << "  Family: " << usage.family << "\n";
            out << "  Size: " << usage.size << "\n";
            out << "  Constructor: " << usage.fullConstructorCall << "\n";
            out << "  Context: " << usage.sourceContext << "\n";
            out << "\n";
        }
        
        out << "==== End Report ====\n";
    }

private:
    QList<FontIconUsage> m_usages;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("FontIcon Collector Console");
    app.setApplicationVersion("1.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("Console version - Collects and displays FontIcon usages from the OpenCloud Desktop codebase");
    parser.addHelpOption();
    parser.addVersionOption();
    
    parser.process(app);
    
    FontIconCollector collector;
    collector.scanForFontIconUsages();
    
    return 0;
}