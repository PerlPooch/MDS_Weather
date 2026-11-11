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
#include <cmath>

Stream Serial;
uint32_t nowMillis = 0;
int enableLevel = 0;
bool failQueue = false;
bool failTask = false;
bool bundleUsed = false;
std::vector<PendingTask> tasks;
HttpState httpState;
static bool networkReady = true;
static bool clockReady = true;

// Return deterministic UTC for cache timestamps.
time_t fakeTime(time_t* result) {
    const time_t currentUtc = 1792944000 + nowMillis / 1000;

    if (result) {
        *result = currentUtc;
    }

    return currentUtc;
}

// Supply valid weather data for successful request scenarios.
static void prepareSuccessfulResponse() {
    httpState.status = 200;
    httpState.size = -1;
    httpState.beginOK = true;
    httpState.payload = R"({"main":{"temp":-12.5},"timezone":-14400,"dt":1792944000,"weather":[{"main":"Snow","description":"light snow"}]})";
}

// Connect the scenario state to each independently owned test service.
static void registerReadinessCallbacks(MDS_Weather& weather) {
    weather.setNetworkReadyCallback([]() {
        // Reflect the scenario's current connectivity.
        return networkReady;
    });
    weather.setClockReadyCallback([]() {
        // Reflect the scenario's current clock validity.
        return clockReady;
    });
}

// Run the queued worker and collect its result.
static void finishRequest(MDS_Weather& weather) {
    runWorker();
    weather.tick();
}

// Cover request gating, caching, retries, and response validation.
int main() {
    MDS_Weather weather;
    registerReadinessCallbacks(weather);
    MDS_Weather::Options options;
    assert(weather.begin(options));
    weather.tick();
    assert(!weather.enabled() && tasks.empty() && httpState.calls == 0);

    options.apiKey = "TESTKEY";
    assert(weather.begin(options));
    weather.tick();
    assert(weather.enabled() && !weather.configured() && tasks.empty());

    options.latitude = "42.5";
    options.longitude = "-71.3";
    assert(weather.begin(options));
    networkReady = false;
    weather.tick();
    networkReady = true;
    clockReady = false;
    weather.tick();
    clockReady = true;
    assert(tasks.empty());

    prepareSuccessfulResponse();
    weather.tick();
    assert(weather.fetching() && tasks.size() == 1 && httpState.calls == 0);
    finishRequest(weather);
    assert(weather.available() && weather.fresh() && bundleUsed);
    assert(weather.httpStatus() == 200 && weather.lastSuccessUtc() == 1792944000);
    assert(weather.sample().temperatureF == -12.5f);
    assert(weather.sample().timezoneOffsetSeconds == -14400);
    assert(String(weather.sample().condition) == "Snow");
    assert(httpState.url.find("https://api.openweathermap.org/data/2.5/weather?") == 0);
    assert(httpState.url.find("units=imperial") != String::npos);
    assert(weather.nextUpdateSeconds() == 600);

    nowMillis = 599999;
    weather.tick();
    assert(tasks.empty());
    nowMillis = 600000;
    weather.tick();
    httpState.status = 500;
    finishRequest(weather);
    assert(weather.available() && weather.nextUpdateSeconds() == 60);
    assert(String(weather.lastError()).find("TESTKEY") == String::npos);

    nowMillis = 660000;
    weather.tick();
    finishRequest(weather);
    assert(weather.nextUpdateSeconds() == 600);

    nowMillis = 1260000;
    weather.tick();
    prepareSuccessfulResponse();
    finishRequest(weather);
    assert(weather.fresh() && weather.lastSuccessUtc() > 1792944000);

    const auto previousSuccessUtc = weather.lastSuccessUtc();
    nowMillis += 600000;
    weather.tick();
    httpState.payload = "{}";
    finishRequest(weather);
    assert(weather.available() && weather.lastSuccessUtc() == previousSuccessUtc);

    // Disabling during a request discards its result and starts no new work.
    nowMillis += 60000;
    weather.tick();
    assert(weather.fetching());
    options.apiKey = "";
    assert(weather.begin(options));
    prepareSuccessfulResponse();
    finishRequest(weather);
    assert(!weather.enabled() && !weather.available() && tasks.empty());

    options.apiKey = "TESTKEY";
    options.intervalSeconds = 60;
    assert(weather.begin(options));
    weather.tick();
    httpState.status = 429;
    finishRequest(weather);
    assert(weather.nextUpdateSeconds() == 600);

    // Reset timing around millis rollover; the due check must still wait 61 seconds.
    options.intervalSeconds = 61;
    nowMillis = 0xfffffff0U;
    assert(weather.begin(options));
    prepareSuccessfulResponse();
    weather.tick();
    finishRequest(weather);
    nowMillis += 60999;
    weather.tick();
    assert(tasks.empty());
    ++nowMillis;
    weather.tick();
    finishRequest(weather);

    // A location edit invalidates old samples and in-flight requests.
    nowMillis += 61000;
    weather.tick();
    options.latitude = "0";
    options.longitude = "0";
    assert(weather.begin(options));
    finishRequest(weather);
    assert(!weather.available() && tasks.size() == 1);
    finishRequest(weather);
    assert(weather.sample().latitude == 0 && weather.sample().longitude == 0);

    JsonDocument value;
    for (const char* invalid : {"91", "nan", " 42", "12junk"}) {
        value["v"] = invalid;
        assert(!MDS_Weather::validLatitude(value["v"]));
    }

    value["v"] = "-180";
    assert(MDS_Weather::validLongitude(value["v"]));
    value["v"] = "";
    assert(MDS_Weather::validLatitude(value["v"]) && MDS_Weather::validApiKey(value["v"]));
    value["v"] = "key&other=value";
    assert(!MDS_Weather::validApiKey(value["v"]));

    nowMillis += 61000;
    weather.tick();
    httpState.size = 20000;
    finishRequest(weather);
    assert(weather.available() && String(weather.lastError()).find("large") != String::npos);
    httpState.size = -1;

    nowMillis += 60000;
    weather.tick();
    httpState.payload = R"({"main":{"temp":70},"timezone":-2147483648,"dt":1792944000,"weather":[{"main":"Clear"}]})";
    finishRequest(weather);
    assert(String(weather.lastError()).find("invalid") != String::npos);

    {
        MDS_Weather failingWeather;
        registerReadinessCallbacks(failingWeather);
        options.intervalSeconds = 600;
        assert(failingWeather.begin(options));
        failQueue = true;
        failingWeather.tick();
        assert(!failingWeather.fetching() && tasks.empty());
        failQueue = false;

        nowMillis += 60000;
        failTask = true;
        failingWeather.tick();
        assert(!failingWeather.fetching() && tasks.empty());
        failTask = false;
    }

    assert(tasks.empty());
    puts("PASS: disabled/missing-location gates, separate worker, verified HTTPS selection, cached parsing, retries/rate limit, rollover, stale-generation rejection, validators, and payload bounds.");
}
