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
#include <QPainter>
#include <QPixmap>

// Simple FontIcon replacement for demonstration
class SimpleFontIcon {
public:
    enum FontFamily { FontAwesome, RemixIcon };
    enum Size { Normal, Half };
    
    SimpleFontIcon(FontFamily family, QChar glyph, Size size)
        : m_family(family), m_glyph(glyph), m_size(size) {}
    
    QPixmap pixmap(const QSize &size) const {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        
        QFont font;
        switch (m_family) {
        case FontAwesome:
            font.setFamily("Font Awesome 6 Free");
            break;
        case RemixIcon:
            font.setFamily("remixicon");
            break;
        }
        
        // Use a generic font as fallback
        if (!QFontDatabase::families().contains(font.family())) {
            font.setFamily(QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
        }
        
        font.setPixelSize(size.height() * 0.8);
        painter.setFont(font);
        painter.setPen(Qt::black);
        
        painter.drawText(QRect(0, 0, size.width(), size.height()), 
                        Qt::AlignCenter, m_glyph);
        
        return pixmap;
    }
    
private:
    FontFamily m_family;
    QChar m_glyph;
    Size m_size;
};

struct FontIconUsage {
    QString filePath;
    int lineNumber;
    QString sourceContext;
    QChar glyph;
    SimpleFontIcon::FontFamily family;
    SimpleFontIcon::Size size;
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

private slots:
    void onUsageSelected(int row)
    {
        if (row < 0 || row >= m_usages.size()) {
            return;
        }

        const auto &usage = m_usages[row];
        
        // Update the glyph display
        SimpleFontIcon icon(usage.family, usage.glyph, usage.size);
        QPixmap pixmap = icon.pixmap(QSize(64, 64));
        m_glyphLabel->setPixmap(pixmap);
        
        // Update the details
        m_detailsText->clear();
        m_detailsText->append(QString("File: %1").arg(usage.filePath));
        m_detailsText->append(QString("Line: %1").arg(usage.lineNumber));
        m_detailsText->append(QString("Glyph: '%1' (Unicode: U+%2)").arg(usage.glyph).arg(usage.glyph.unicode(), 4, 16, QChar('0')).toUpper());
        m_detailsText->append(QString("Family: %1").arg(usage.family == SimpleFontIcon::FontAwesome ? "FontAwesome" : "RemixIcon"));
        m_detailsText->append(QString("Size: %1").arg(usage.size == SimpleFontIcon::Normal ? "Normal" : "Half"));
        m_detailsText->append("");
        m_detailsText->append("Full Constructor Call:");
        m_detailsText->append(usage.fullConstructorCall);
        m_detailsText->append("");
        m_detailsText->append("Source Context:");
        m_detailsText->append(usage.sourceContext);
    }

private:
    void setupUI()
    {
        setWindowTitle("FontIcon Usage Collector - OpenCloud Desktop");
        resize(1200, 800);

        auto *layout = new QHBoxLayout(this);
        auto *splitter = new QSplitter(Qt::Horizontal, this);
        
        // Left side: List of usages
        auto *leftWidget = new QWidget;
        auto *leftLayout = new QVBoxLayout(leftWidget);
        
        auto *titleLabel = new QLabel("FontIcon Usages Found:");
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 2);
        titleLabel->setFont(titleFont);
        leftLayout->addWidget(titleLabel);
        
        m_usagesList = new QListWidget;
        leftLayout->addWidget(m_usagesList);
        
        connect(m_usagesList, &QListWidget::currentRowChanged, this, &FontIconCollectorWidget::onUsageSelected);
        
        // Right side: Details and glyph display
        auto *rightWidget = new QWidget;
        auto *rightLayout = new QVBoxLayout(rightWidget);
        
        // Glyph display
        auto *glyphGroup = new QGroupBox("Glyph Preview");
        auto *glyphLayout = new QVBoxLayout(glyphGroup);
        
        m_glyphLabel = new QLabel;
        m_glyphLabel->setAlignment(Qt::AlignCenter);
        m_glyphLabel->setStyleSheet("border: 2px solid #cccccc; padding: 20px; background-color: white;");
        m_glyphLabel->setMinimumSize(120, 120);
        m_glyphLabel->setText("Select a usage to\npreview the glyph");
        glyphLayout->addWidget(m_glyphLabel);
        
        // Details text
        auto *detailsGroup = new QGroupBox("Usage Details");
        auto *detailsLayout = new QVBoxLayout(detailsGroup);
        
        m_detailsText = new QTextEdit;
        m_detailsText->setReadOnly(true);
        QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        monoFont.setPointSize(10);
        m_detailsText->setFont(monoFont);
        detailsLayout->addWidget(m_detailsText);
        
        rightLayout->addWidget(glyphGroup);
        rightLayout->addWidget(detailsGroup);
        
        splitter->addWidget(leftWidget);
        splitter->addWidget(rightWidget);
        splitter->setSizes({400, 800});
        
        layout->addWidget(splitter);
        
        // Set a nice status text initially
        m_detailsText->setPlainText("Welcome to the FontIcon Usage Collector!\n\nThis tool scans the OpenCloud Desktop codebase and finds all uses of FontIcon constructors.\n\nSelect an item from the list on the left to see details and a preview of the glyph.");
    }

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
            m_detailsText->setPlainText("Error: Could not find src directory!\nPlease run this tool from the OpenCloud Desktop root directory.");
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
        
        // Populate the list widget
        for (const auto &usage : m_usages) {
            QString displayText = QString("%1:%2 - '%3' (%4)")
                .arg(QFileInfo(usage.filePath).fileName())
                .arg(usage.lineNumber)
                .arg(usage.glyph)
                .arg(usage.family == SimpleFontIcon::FontAwesome ? "FA" : "RI");
            m_usagesList->addItem(displayText);
        }
        
        if (m_usages.isEmpty()) {
            m_detailsText->setPlainText("No FontIcon usages found in the source code.\n\nMake sure you're running this from the OpenCloud Desktop root directory and that the source files contain FontIcon constructor calls.");
        } else {
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
                    QString familyStr = familyMatch.captured(1);
                    usage.family = (familyStr == "RemixIcon") ? SimpleFontIcon::RemixIcon : SimpleFontIcon::FontAwesome;
                } else {
                    usage.family = SimpleFontIcon::FontAwesome;
                }
                
                // Extract size (default to Normal)
                QRegularExpression sizeRegex(R"(FontIcon::Size::(\w+))");
                auto sizeMatch = sizeRegex.match(constructorContent);
                if (sizeMatch.hasMatch()) {
                    QString sizeStr = sizeMatch.captured(1);
                    usage.size = (sizeStr == "Half") ? SimpleFontIcon::Half : SimpleFontIcon::Normal;
                } else {
                    usage.size = SimpleFontIcon::Normal;
                }
                
                // Get context (current line and a few lines around it)
                QStringList contextLines;
                for (int ctx = qMax(0, lineNum - 2); ctx <= qMin(lines.size() - 1, lineNum + 2); ++ctx) {
                    QString prefix = (ctx == lineNum) ? ">>> " : "    ";
                    contextLines << QString("%1%2: %3").arg(prefix).arg(ctx + 1, 3).arg(lines[ctx]);
                }
                usage.sourceContext = contextLines.join('\n');
                
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
    app.setApplicationName("FontIcon Collector");
    app.setApplicationVersion("1.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("Collects and displays FontIcon usages from the OpenCloud Desktop codebase");
    parser.addHelpOption();
    parser.addVersionOption();
    
    parser.process(app);
    
    FontIconCollectorWidget window;
    window.show();
    
    return app.exec();
}

#include "fonticon_collector_simple.moc"