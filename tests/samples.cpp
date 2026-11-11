/*
 * ===========================================================================
 * MDS_Weather - https://home.marcspencer.com/mds-weather
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

// Hold UTC constant so timestamp equality cannot hide accepted updates.
time_t fakeTime(time_t* result) {
    const time_t currentUtc = 1792944000;

    if (result) {
        *result = currentUtc;
    }

    return currentUtc;
}

// Complete a due request and verify the worker cannot publish the version itself.
static void completeRequest(MDS_Weather& weather) {
    nowMillis += 600000;
    const uint32_t previousSampleVersion = weather.sampleVersion();
    weather.tick();
    assert(weather.fetching());
    assert(weather.sampleVersion() == previousSampleVersion);

    runWorker();
    assert(weather.sampleVersion() == previousSampleVersion);
    weather.tick();
    assert(!weather.fetching());
}

// Count accepted samples independently of timestamps, configuration, and failures.
int main() {
    bool networkReady = true;
    uint32_t acceptedNotifications = 0;
    MDS_Weather weather;
    MDS_Weather::Options options{"TESTKEY", "42.5", "-71.3", 60};

    weather.setNetworkReadyCallback([&]() {
        // Allow stale-result scenarios to suspend new work while collecting results.
        return networkReady;
    });
    weather.setClockReadyCallback([]() {
        // Treat the fixed UTC fixture as usable for TLS.
        return true;
    });
    weather.setStatusCallback([&](const String& message) {
        // The accepted counter must already be visible to update observers.
        if (message == "Weather: updated") {
            ++acceptedNotifications;
            assert(weather.sampleVersion() == acceptedNotifications);
        }
    });

    assert(weather.sampleVersion() == 0);
    assert(weather.begin(options));
    assert(weather.sampleVersion() == 0);
    httpState.status = 200;
    httpState.size = -1;
    httpState.beginOK = true;
    httpState.payload = R"({"main":{"temp":70},"timezone":0,"dt":1792944000,"weather":[{"main":"Clear"}]})";
    completeRequest(weather);
    assert(weather.sampleVersion() == 1);
    const time_t firstSuccessUtc = weather.lastSuccessUtc();

    completeRequest(weather);
    assert(weather.sampleVersion() == 2);
    assert(weather.lastSuccessUtc() == firstSuccessUtc);
    weather.tick();
    assert(weather.sampleVersion() == 2);
    assert(weather.begin(options));
    assert(weather.sampleVersion() == 2);

    httpState.status = 500;
    completeRequest(weather);
    assert(weather.sampleVersion() == 2 && weather.available());
    httpState.status = 200;

    // Reconfiguration invalidates an in-flight successful result without counting it.
    nowMillis += 600000;
    weather.tick();
    assert(weather.fetching());
    options.latitude = "43";
    assert(weather.begin(options));
    assert(!weather.available() && weather.sampleVersion() == 2);
    networkReady = false;
    runWorker();
    weather.tick();
    assert(!weather.fetching() && weather.sampleVersion() == 2);
    networkReady = true;
    completeRequest(weather);
    assert(weather.sampleVersion() == 3);

    // Disabling and restoring the service must preserve the instance counter.
    nowMillis += 600000;
    weather.tick();
    assert(weather.fetching());
    options.apiKey = "";
    assert(weather.begin(options));
    runWorker();
    weather.tick();
    assert(!weather.available() && weather.sampleVersion() == 3);
    options.apiKey = "TESTKEY";
    assert(weather.begin(options));
    completeRequest(weather);
    assert(weather.sampleVersion() == 4);

    options.latitude = "91";
    assert(!weather.begin(options));
    assert(weather.sampleVersion() == 4 && acceptedNotifications == 4);
    assert(tasks.empty());
    puts("PASS: accepted sample versions, identical timestamps/data, notification ordering, failures and stale results.");
}
