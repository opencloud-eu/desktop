/*
 * Copyright (C) by Hannah von Reth <hannah.vonreth@owncloud.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "chronoelapsedtimer.h"

#include <QtGlobal>

using namespace OCC::Utility;

ChronoElapsedTimer::ChronoElapsedTimer(bool start)
    : _start(std::chrono::steady_clock::now())
    , _started(start)
{
}

bool ChronoElapsedTimer::isStarted() const
{
    return _started;
}

void ChronoElapsedTimer::reset()
{
    _start = std::chrono::steady_clock::now();
    _end = {};
    _started = true;
}

void ChronoElapsedTimer::stop()
{
    Q_ASSERT(_end == std::chrono::steady_clock::time_point{});
    Q_ASSERT(_started);
    _end = std::chrono::steady_clock::now();
    _started = false;
}

std::chrono::nanoseconds ChronoElapsedTimer::duration() const
{
    if (!_started) {
        return std::chrono::nanoseconds::max();
    }
    if (_end != std::chrono::steady_clock::time_point{}) {
        return _end - _start;
    }
    return std::chrono::steady_clock::now() - _start;
}
