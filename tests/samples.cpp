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
    uint32_t updateNotifications = 0;
    MDS_Weather::Sample copiedSample;
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
    weather.setUpdateCallback([&](const MDS_Weather::Sample& sample) {
        // Copy values during the call without retaining the borrowed reference.
        ++updateNotifications;
        copiedSample = sample;
    });
    weather.setStatusCallback([&](const String& message) {
        // The accepted counter must already be visible to update observers.
        if (message == "Weather: updated") {
            ++acceptedNotifications;
            assert(weather.sampleVersion() == acceptedNotifications);
            assert(updateNotifications == acceptedNotifications);
            assert(weather.available() && !weather.fetching());
            assert(weather.error() == MDS_Weather::Error::None);
            assert(weather.lastSuccessUtc() == 1792944000);
            assert(weather.nextUpdateSeconds() == options.intervalSeconds);
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
    assert(weather.sampleVersion() == 1 && updateNotifications == 1);
    assert(copiedSample.temperatureF == 70 && copiedSample.timezoneOffsetSeconds == 0);
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
    assert(weather.sampleVersion() == 2 && weather.available() && updateNotifications == 2);
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

    assert(updateNotifications == 4);

    // Callback registration is independent of errors, configuration and the counter.
    weather.setStatusCallback(nullptr);
    weather.setUpdateCallback(nullptr);
    completeRequest(weather);
    assert(weather.sampleVersion() == 5 && updateNotifications == 4);

    uint32_t replacementNotifications = 0;
    weather.setUpdateCallback([&](const MDS_Weather::Sample&) {
        // A replacement receives subsequent accepted updates exclusively.
        ++replacementNotifications;
    });
    completeRequest(weather);
    assert(weather.sampleVersion() == 6 && updateNotifications == 4);
    assert(replacementNotifications == 1);

    nowMillis += 600000;
    weather.tick();
    assert(weather.fetching());
    runWorker();
    assert(replacementNotifications == 1);
    weather.setUpdateCallback(nullptr);
    weather.tick();
    assert(weather.sampleVersion() == 7 && replacementNotifications == 1);

    options.latitude = "91";
    assert(!weather.begin(options));
    assert(weather.sampleVersion() == 7 && acceptedNotifications == 4);
    weather.setUpdateCallback(nullptr);
    assert(weather.error() == MDS_Weather::Error::InvalidOptions);
    assert(tasks.empty());
    puts("PASS: sample versions and optional update callbacks, committed state, ordering, replacement/clearing, failures and stale results.");
}
