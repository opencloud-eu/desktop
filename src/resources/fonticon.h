// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "opencloudresourceslib.h"

#include <QIcon>
#include <QtQmlIntegration/QtQmlIntegration>

namespace OCC::Resources {
class OPENCLOUD_RESOURCES_EXPORT FontIcon : public QIcon
{
    Q_GADGET
    QML_VALUE_TYPE(fontIcon)
public:
    enum class DefaultGlyphes : char16_t {
        Question = u'',
        Warning = u'',
        Info = u''

    };
    Q_ENUM(DefaultGlyphes);

    enum class FontFamily : uint8_t {
        FontAwesome,
        RemixIcon,
    };
    Q_ENUM(FontFamily);

    enum class Size : uint8_t {
        // fullsized icon
        Normal,
        // hafl sized, centered icon
        Half
    };
    Q_ENUM(Size);
    FontIcon();
    // defaults to fontawesoem
    FontIcon(QChar glyphe, Size size = Size::Normal, const QColor &color = {});
    FontIcon(DefaultGlyphes glyphe, Size size = Size::Normal, const QColor &color = {});
    FontIcon(FontFamily family, QChar glyphe, Size size = Size::Normal, const QColor &color = {});
};

// expose the enums to qml
class FontIconDerived : public FontIcon
{
    Q_GADGET
};

namespace FontIconDerivedForeign {
    Q_NAMESPACE
    QML_NAMED_ELEMENT(FontIcon)
    QML_FOREIGN_NAMESPACE(Resources::FontIconDerived)
}
}
