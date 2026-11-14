/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "opencloudsynclib.h"

#include <QObject>
#include <memory>

namespace OCC {

// A null activity means that a rotating token cannot safely be exchanged yet.
OPENCLOUD_SYNC_EXPORT std::shared_ptr<QObject> createTokenRefreshActivity();

}
