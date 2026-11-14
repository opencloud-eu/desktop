/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "refreshactivity.h"

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include <QLoggingCategory>
#include <QScopeGuard>

#include <algorithm>
#include <vector>

Q_LOGGING_CATEGORY(lcRefreshActivity, "sync.credentials.activity")

namespace {
constexpr CFTimeInterval activityTimeout = 60;

class RefreshActivity : public QObject
{
public:
    ~RefreshActivity() override
    {
        if (_idleAssertion != kIOPMNullAssertionID) {
            IOPMAssertionRelease(_idleAssertion);
        }
        if (_networkAssertion != kIOPMNullAssertionID) {
            IOPMAssertionRelease(_networkAssertion);
        }
    }

    bool acquire()
    {
        // Idle assertions alone do not protect DarkWake. A network assertion
        // covers maintenance wakes on AC power without waking the display.
        return createAssertion(kIOPMAssertPreventUserIdleSystemSleep, &_idleAssertion) && createAssertion(kIOPMAssertNetworkClientActive, &_networkAssertion);
    }

private:
    static bool createAssertion(CFStringRef type, IOPMAssertionID *id)
    {
        const auto result = IOPMAssertionCreateWithDescription(
            type, CFSTR("OpenCloud OAuth token refresh"), nullptr, nullptr, nullptr, activityTimeout, kIOPMAssertionTimeoutActionTurnOff, id);
        if (result != kIOReturnSuccess) {
            qCWarning(lcRefreshActivity) << "Could not protect token refresh from sleep:" << result;
            return false;
        }
        return true;
    }

    IOPMAssertionID _idleAssertion = kIOPMNullAssertionID;
    IOPMAssertionID _networkAssertion = kIOPMNullAssertionID;
};

bool hasAwakeDisplay()
{
    uint32_t count = 0;
    if (CGGetOnlineDisplayList(0, nullptr, &count) != kCGErrorSuccess || count == 0) {
        return false;
    }
    std::vector<CGDirectDisplayID> displays(count);
    if (CGGetOnlineDisplayList(count, displays.data(), &count) != kCGErrorSuccess) {
        return false;
    }
    return std::any_of(displays.begin(), displays.begin() + count, [](auto display) { return !CGDisplayIsAsleep(display); });
}
}
#endif

std::shared_ptr<QObject> OCC::createTokenRefreshActivity()
{
#ifdef Q_OS_MACOS
    auto activity = std::make_shared<RefreshActivity>();
    if (!activity->acquire()) {
        return {};
    }
    const auto powerInfo = IOPSCopyPowerSourcesInfo();
    if (!powerInfo) {
        return {};
    }
    const QScopeGuard releasePowerInfo([powerInfo] { CFRelease(powerInfo); });
    const auto source = IOPSGetProvidingPowerSourceType(powerInfo);
    if (!source) {
        return {};
    }
    const bool onAC = CFEqual(source, CFSTR(kIOPSACPowerValue));
    // On battery, assertions cannot guarantee a DarkWake stays awake. Defer
    // rather than consume a one-use refresh token and lose its replacement.
    if (!onAC && !hasAwakeDisplay()) {
        return {};
    }
    return activity;
#else
    return std::make_shared<QObject>();
#endif
}
