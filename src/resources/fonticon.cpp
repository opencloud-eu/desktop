// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "fonticon.h"

#include "resources.h"

#include <QIconEngine>
#include <QPainter>
#include <QPixmapCache>

using namespace OCC::Resources;

namespace {

class FontIconEngine : public QIconEngine
{
public:
    FontIconEngine(FontIcon::FontFamily family, QChar glyph, FontIcon::Size size, const QColor &color)
        : _family(family)
        , _glyph(glyph)
        , _size(size)
        , _color(color.isValid() ? color : OCC::Resources::tint())
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode, QIcon::State) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        auto pen = painter->pen();
        pen.setColor(_color);
        painter->setPen(pen);

        auto font = painter->font();
        switch (_family) {
        case FontIcon::FontFamily::FontAwesome:
            font.setFamily(QStringLiteral("Font Awesome 6 Free"));
            break;
        case FontIcon::FontFamily::RemixIcon:
            font.setFamily(QStringLiteral("remixicon"));
            break;
        }

        switch (_size) {
        case FontIcon::Size::Normal:
            font.setPixelSize(rect.height());
            break;
        case FontIcon::Size::Half:
            font.setPixelSize(rect.height() / 2.0);
            break;
        }

        // inspired by https://github.com/gamecreature/QtAwesome/
        const auto flags = Qt::AlignHCenter | Qt::AlignVCenter;
        QFontMetricsF metrics(font);
        QRectF boundingRect = metrics.boundingRect(rect, flags, _glyph);
        if (boundingRect.width() > rect.width()) {
            auto drawSize =
                static_cast<int>(font.pixelSize() * qMin(rect.width() * 0.95 / boundingRect.width(), metrics.height() * 0.95 / boundingRect.height()));
            font.setPixelSize(drawSize);
        }

        painter->setFont(font);
        painter->drawText(rect, flags, _glyph);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pixmap;
        const QString key = pixmapKey(size, mode, state);
        if (QPixmapCache::find(key, &pixmap)) {
            return pixmap;
        }

        pixmap = QPixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, {{0, 0}, size}, mode, state);
        painter.end();
        QPixmapCache::insert(key, pixmap);
        return pixmap;
    }

    QIconEngine *clone() const override { return new FontIconEngine(this->_family, this->_glyph, this->_size, this->_color); }

    QString pixmapKey(const QSize &size, QIcon::Mode mode, QIcon::State state)
    {
        return QStringLiteral("FontIcon:%1").arg(QString::number(qHashMulti(0, _family, _glyph, _size, _color.rgb(), size, mode, state), 16));
    }

    const FontIcon::FontFamily _family;
    const QChar _glyph;
    const FontIcon::Size _size;
    const QColor _color;
};
}


FontIcon::FontIcon() { }

FontIcon::FontIcon(QChar glyphe, Size size, const QColor &color)
    : QIcon(new FontIconEngine(FontFamily::FontAwesome, glyphe, size, color))
{
}
FontIcon::FontIcon(DefaultGlyphes glyphe, Size size, const QColor &color)
    : FontIcon(static_cast<std::underlying_type_t<DefaultGlyphes>>(glyphe), size, color)
{
}

FontIcon::FontIcon(FontFamily family, QChar glyphe, Size size, const QColor &color)
    : QIcon(new FontIconEngine(family, glyphe, size, color))
{
}
