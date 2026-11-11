/*
 * ===========================================================================
 * MDS_Weather 0.1.0 - https://home.marcspencer.com/mds-weather
 *
 * Copyright (c) 2026 Marc D. Spencer.
 * All Rights Reserved under the applicable license.
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 * ===========================================================================
 */

#include <MDS_Weather.h>
#include <HTTPClient.h>
#include <freertos/task.h>
#include <cassert>
#include <cstdio>

Stream Serial;
uint32_t nowMillis = 0;
int enableLevel = 0;
bool failQueue = false;
bool failTask = false;
bool bundleUsed = false;
std::vector<PendingTask> tasks;
HttpState httpState;

// Keep the UTC clock deterministic as simulated milliseconds advance.
time_t fakeTime(time_t* result) {
    const time_t currentUtc = 1792944000 + nowMillis / 1000;

    if (result) {
        *result = currentUtc;
    }

    return currentUtc;
}

// Verify safe defaults, live readiness, callback removal, and pending-result handling.
int main() {
    bool networkReady = false;
    bool clockReady = false;
    int networkQueries = 0;
    int clockQueries = 0;
    MDS_Weather weather;
    MDS_Weather::Options options{"TESTKEY", "42.5", "-71.3", 600};

    assert(weather.begin(options));
    weather.tick();
    assert(String(weather.state()) == "waiting_network");
    assert(weather.error() == MDS_Weather::Error::None && tasks.empty());

    weather.setClockReadyCallback([&]() {
        // Count evaluations even when the network prerequisite is false.
        ++clockQueries;
        return clockReady;
    });
    weather.tick();
    assert(clockQueries == 1 && tasks.empty());

    weather.setNetworkReadyCallback([&]() {
        // Reflect connectivity changes without re-registering the callback.
        ++networkQueries;
        return networkReady;
    });
    weather.tick();
    assert(networkQueries == 1 && clockQueries == 2 && tasks.empty());

    networkReady = true;
    weather.tick();
    assert(String(weather.state()) == "waiting_time" && tasks.empty());

    weather.setClockReadyCallback(nullptr);
    weather.tick();
    assert(String(weather.state()) == "waiting_time" && tasks.empty());
    assert(clockQueries == 3);

    weather.setClockReadyCallback([&]() {
        // Expose a usable clock independently of connectivity.
        ++clockQueries;
        return clockReady;
    });
    clockReady = true;
    httpState.status = 200;
    httpState.size = -1;
    httpState.beginOK = true;
    httpState.payload = R"({"main":{"temp":70},"timezone":0,"dt":1792944000,"weather":[{"main":"Clear"}]})";
    weather.tick();
    assert(weather.fetching() && tasks.size() == 1);

    // Clearing providers must not prevent the outstanding result from being accepted.
    weather.setNetworkReadyCallback(nullptr);
    weather.setClockReadyCallback(nullptr);
    runWorker();
    weather.tick();
    assert(!weather.fetching() && weather.available() && tasks.empty());
    assert(String(weather.state()) == "waiting_network");

    nowMillis += 600000;
    weather.tick();
    assert(weather.available() && weather.ageSeconds() == 600 && tasks.empty());
    assert(weather.begin(options));
    assert(weather.error() == MDS_Weather::Error::None);

    weather.setNetworkReadyCallback([&]() {
        // Restore a live connection source after clearing it.
        return networkReady;
    });
    weather.tick();
    assert(String(weather.state()) == "waiting_time" && tasks.empty());

    weather.setClockReadyCallback([&]() {
        // Restore the time source and allow the due request to proceed.
        return clockReady;
    });
    weather.tick();
    assert(weather.fetching());
    httpState.status = 500;
    runWorker();
    weather.setClockReadyCallback(nullptr);
    weather.tick();
    assert(weather.error() == MDS_Weather::Error::RequestFailed);
    assert(weather.available() && !weather.fetching());

    nowMillis += 60000;
    weather.tick();
    assert(weather.error() == MDS_Weather::Error::RequestFailed && tasks.empty());

    puts("PASS: Weather unset, partial, changing and cleared readiness callbacks; pending results and errors.");
}
