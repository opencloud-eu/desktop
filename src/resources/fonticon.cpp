// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "fonticon.h"

#include "resources.h"

#include <QIconEngine>
#include <QPainter>

using namespace OCC::Resources;

namespace {

class FontIconEngine : public QIconEngine
{
public:
    FontIconEngine(FontIcon::FontFamily family, QChar glyph, FontIcon::Size size)
        : _family(family)
        , _glyph(glyph)
        , _size(size)
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode, QIcon::State) override
    {
        painter->save();
        painter->fillRect(rect, Qt::transparent);
        painter->setRenderHint(QPainter::Antialiasing);

        auto pen = painter->pen();
        pen.setColor(OCC::Resources::tint());
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
        painter->setFont(font);

        painter->drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, _glyph);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent); // we need transparency
        QPainter painter(&pixmap);
        paint(&painter, {{0, 0}, size}, mode, state);
        painter.end();
        return pixmap;
    }

    QIconEngine *clone() const override { return new FontIconEngine(this->_family, this->_glyph, this->_size); }

    const FontIcon::FontFamily _family;
    const QChar _glyph;
    const FontIcon::Size _size;
};
}


FontIcon::FontIcon() { }

FontIcon::FontIcon(QChar glyphe, Size size)
    : QIcon(new FontIconEngine(FontFamily::FontAwesome, glyphe, size))
{
}

FontIcon::FontIcon(FontFamily family, QChar glyphe, Size size)
    : QIcon(new FontIconEngine(family, glyphe, size))
{
}
