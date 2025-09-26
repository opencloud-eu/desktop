// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 OpenCloud FontIcon Collector Tool

#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTextEdit>
#include <QLabel>
#include <QSplitter>
#include <QScrollArea>
#include <QGridLayout>
#include <QFrame>
#include <QDir>
#include <QDirIterator>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QFont>
#include <QCommandLineParser>
#include <QGroupBox>
#include <QFontDatabase>

#include "fonticon.h"

using namespace Qt::Literals::StringLiterals;
using namespace OCC::Resources;

struct FontIconUsage {
    QString filePath;
    int lineNumber;
    QString sourceContext;
    QChar glyph;
    FontIcon::FontFamily family;
    FontIcon::Size size;
    QString fullConstructorCall;
};

class FontIconCollectorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FontIconCollectorWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setupUI();
        scanForFontIconUsages();
    }

private Q_SLOTS:
    void onUsageSelected(int row)
    {
        if (row < 0 || row >= m_usages.size()) {
            return;
        }

        const auto &usage = m_usages[row];
        
        // Update the glyph display
        FontIcon icon(usage.family, usage.glyph, usage.size);
        QPixmap pixmap = icon.pixmap(QSize(64, 64));
        m_glyphLabel->setPixmap(pixmap);
        
        // Update the details
        m_detailsText->clear();
        m_detailsText->append(u"File: %1"_s.arg(usage.filePath));
        m_detailsText->append(u"Line: %1"_s.arg(usage.lineNumber));
        m_detailsText->append(u"Glyph: %1 (0x%2)"_s.arg(usage.glyph).arg(static_cast<uint16_t>(usage.glyph.unicode()), 4, 16, QChar('0'_L1)));
        m_detailsText->append(u"Family: %1"_s.arg(usage.family == FontIcon::FontFamily::FontAwesome ? u"FontAwesome"_s : u"RemixIcon"_s));
        m_detailsText->append(u"Size: %1"_s.arg(usage.size == FontIcon::Size::Normal ? u"Normal"_s : u"Half"_s));
        m_detailsText->append(u"\n"_s);
        m_detailsText->append(u"Context:"_s);
        m_detailsText->append(usage.sourceContext);
    }

private:
    void setupUI()
    {
        setWindowTitle(u"FontIcon Usage Collector"_s);
        resize(1200, 800);

        auto *layout = new QHBoxLayout(this);
        auto *splitter = new QSplitter(Qt::Horizontal, this);
        
        // Left side: List of usages
        auto *leftWidget = new QWidget;
        auto *leftLayout = new QVBoxLayout(leftWidget);

        leftLayout->addWidget(new QLabel(u"FontIcon Usages Found:"_s));
        m_usagesList = new QListWidget;
        leftLayout->addWidget(m_usagesList);
        
        connect(m_usagesList, &QListWidget::currentRowChanged, this, &FontIconCollectorWidget::onUsageSelected);
        
        // Right side: Details and glyph display
        auto *rightWidget = new QWidget;
        auto *rightLayout = new QVBoxLayout(rightWidget);
        
        // Glyph display
        auto *glyphGroup = new QGroupBox(u"Glyph Preview"_s);
        auto *glyphLayout = new QVBoxLayout(glyphGroup);
        
        m_glyphLabel = new QLabel;
        m_glyphLabel->setAlignment(Qt::AlignCenter);
        m_glyphLabel->setStyleSheet(u"border: 1px solid gray; padding: 10px;"_s);
        m_glyphLabel->setMinimumSize(100, 100);
        glyphLayout->addWidget(m_glyphLabel);
        
        // Details text
        auto *detailsGroup = new QGroupBox(u"Details"_s);
        auto *detailsLayout = new QVBoxLayout(detailsGroup);
        
        m_detailsText = new QTextEdit;
        m_detailsText->setReadOnly(true);
        m_detailsText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        detailsLayout->addWidget(m_detailsText);
        
        rightLayout->addWidget(glyphGroup);
        rightLayout->addWidget(detailsGroup);
        
        splitter->addWidget(leftWidget);
        splitter->addWidget(rightWidget);
        splitter->setSizes({400, 800});
        
        layout->addWidget(splitter);
    }

    void scanForFontIconUsages()
    {
        QDir srcDir = QDir(QStringLiteral(SRC_DIR));


        if (!srcDir.exists()) {
            qWarning() << "Could not find src directory";
            return;
        }
        
        qDebug() << "Scanning directory:" << srcDir.absolutePath();
        
        // Recursively scan for C++ files
        QDirIterator it(srcDir.absolutePath(), QStringList() << u"*.cpp"_s << u"*.h"_s, QDir::Files, QDirIterator::Subdirectories);

        while (it.hasNext()) {
            QString filePath = it.next();
            scanFile(filePath);
        }
        
        qDebug() << "Found" << m_usages.size() << "FontIcon usages";
        
        // Populate the list widget
        for (const auto &usage : m_usages) {
            QString displayText = u"%1:%2 - %3"_s.arg(QFileInfo(usage.filePath).fileName(), QString::number(usage.lineNumber), usage.glyph);
            m_usagesList->addItem(displayText);
        }
        
        if (!m_usages.isEmpty()) {
            m_usagesList->setCurrentRow(0);
        }
    }

    void scanFile(const QString &filePath)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return;
        }
        
        QTextStream in(&file);
        QString content = in.readAll();
        QStringList lines = content.split('\n'_L1);

        // Regular expressions to match FontIcon constructor calls
        QRegularExpression fontIconRegex(uR"(FontIcon\s*\(\s*([^)]+)\s*\))"_s);
        QRegularExpression glyphRegex(uR"(u'(.)')"_s);
        QRegularExpression familyRegex(uR"(FontIcon::FontFamily::(\w+))"_s);
        QRegularExpression sizeRegex(uR"(FontIcon::Size::(\w+))"_s);

        for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
            const QString &line = lines[lineNum];
            
            auto fontIconMatch = fontIconRegex.match(line);
            if (fontIconMatch.hasMatch()) {
                FontIconUsage usage;
                usage.filePath = filePath;
                usage.lineNumber = lineNum + 1;
                usage.fullConstructorCall = fontIconMatch.captured(0);
                
                // Extract glyph
                auto glyphMatch = glyphRegex.match(line);
                if (glyphMatch.hasMatch()) {
                    QString glyphStr = glyphMatch.captured(1);
                    if (!glyphStr.isEmpty()) {
                        usage.glyph = glyphStr[0];
                    }
                } else {
                    // Look for hex unicode like u'\uf015'
                    QRegularExpression hexGlyphRegex(uR"(u'\\u([0-9a-fA-F]{4})')"_s);
                    auto hexMatch = hexGlyphRegex.match(line);
                    if (hexMatch.hasMatch()) {
                        bool ok;
                        uint unicode = hexMatch.captured(1).toUInt(&ok, 16);
                        if (ok) {
                            usage.glyph = QChar(unicode);
                        }
                    } else {
                        // Try to find the glyph directly in the line
                        QRegularExpression directGlyphRegex(uR"(u'(.)')"_s);
                        auto directMatch = directGlyphRegex.match(line);
                        if (directMatch.hasMatch()) {
                            usage.glyph = directMatch.captured(1)[0];
                        } else {
                            usage.glyph = QChar('?'_L1); // Unknown glyph
                        }
                    }
                }
                
                // Extract family (default to FontAwesome)
                auto familyMatch = familyRegex.match(line);
                if (familyMatch.hasMatch()) {
                    QString familyStr = familyMatch.captured(1);
                    usage.family = (familyStr == u"RemixIcon"_s) ? FontIcon::FontFamily::RemixIcon : FontIcon::FontFamily::FontAwesome;
                } else {
                    usage.family = FontIcon::FontFamily::FontAwesome;
                }
                
                // Extract size (default to Normal)
                auto sizeMatch = sizeRegex.match(line);
                if (sizeMatch.hasMatch()) {
                    QString sizeStr = sizeMatch.captured(1);
                    usage.size = (sizeStr == u"Half"_s) ? FontIcon::Size::Half : FontIcon::Size::Normal;
                } else {
                    usage.size = FontIcon::Size::Normal;
                }
                
                // Get context (current line and a few lines around it)
                QStringList contextLines;
                for (int ctx = qMax(0, lineNum - 2); ctx <= qMin(lines.size() - 1, lineNum + 2); ++ctx) {
                    QString prefix = (ctx == lineNum) ? u">>> "_s : u"    "_s;
                    contextLines << u"%1%2: %3"_s.arg(prefix, QString::number(ctx + 1), lines[ctx]);
                }
                usage.sourceContext = contextLines.join('\n'_L1);

                m_usages.append(usage);
            }
        }
    }

private:
    QListWidget *m_usagesList = nullptr;
    QLabel *m_glyphLabel = nullptr;
    QTextEdit *m_detailsText = nullptr;
    QList<FontIconUsage> m_usages;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(u"FontIcon Collector"_s);
    app.setApplicationVersion(u"1.0"_s);

    const QString cascadiaCodeFontPath = u"Cascadia Code NF"_s;
    if (QFontDatabase::hasFamily(cascadiaCodeFontPath)) {
        QApplication::setFont(cascadiaCodeFontPath);
    }
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Collects and displays FontIcon usages from the OpenCloud Desktop codebase"_s);
    parser.addHelpOption();
    parser.addVersionOption();
    
    parser.process(app);
    
    FontIconCollectorWidget window;
    window.show();
    
    return app.exec();
}

#include "fonticon_collector.moc"
