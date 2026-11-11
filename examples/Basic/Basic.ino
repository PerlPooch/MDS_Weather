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

#include <MDS_WiFi.h>
#include <time.h>
#include <MDS_Weather.h>

MDS_WiFi wifi;
MDS_Weather weather;

// Configure connectivity, readiness sources, and service options.
void setup() {
    Serial.begin(115200);

    // You could use boring WiFi.begin("YOUR_SSID", "YOUR_PASSWORD") instead,
    // but then you'd have to hardcode your SSID and password here ...
    MDS_WiFi::Options wifiOptions;

    wifiOptions.accessPointPrefix = "Weather";
    wifiOptions.connectionPolicy =
        MDS_WiFi::ConnectionPolicy::RetrySavedNetworkAsync;

    wifi.begin(wifiOptions);
    
    // HTTPS certificate verification requires a valid clock; NTP supplies it.
    configTime(0, 0, "pool.ntp.org");  // Synchronize the system clock to UTC.

    weather.setStatusCallback([](const String& message) {
        Serial.println(message);
    });

    weather.setNetworkReadyCallback([]() {
        // Use the connection manager's station and IP readiness.
        return wifi.ready();
    });

    weather.setClockReadyCallback([]() {
        // A plausible UTC clock permits TLS certificate date validation.
        return time(nullptr) > 1577836800;
    });

    MDS_Weather::Options options;
    
    options.apiKey = "YOURAPIKEY";
    options.latitude = "40.7128";
    options.longitude = "-74.0060";
    options.intervalSeconds = 600;

    if (!weather.begin(options)) {
        Serial.println(weather.lastError());
    }
}

// Service background work and report newly available data.
void loop() {
    wifi.tick();

    weather.tick();

    // Print each successful update once.
    static time_t lastPrinted = 0;
    
    if (weather.available() && weather.lastSuccessUtc() != lastPrinted) {
        lastPrinted = weather.lastSuccessUtc();
        const auto& sample = weather.sample();
        
        Serial.print(sample.temperatureF, 1);
        Serial.print(" F, ");
        Serial.println(sample.description);
    }
}
