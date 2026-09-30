// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include <QtOpenApiCommon/qoaihelpers.h>

namespace QtOpenApiCommon {
template <typename T>
QString serializeArrayValue(const QSet<T> &value, const SerializationOptions &opts)
{
    return serializeArrayValue(QList<T>(value.cbegin(), value.cend()), opts);
}
}
