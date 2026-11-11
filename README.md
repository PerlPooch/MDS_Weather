# MDS_Weather 0.4.0

Background OpenWeather current conditions over verified HTTPS, with cached results and scheduled retries.

## Migration from 0.1.0

Register `setNetworkReadyCallback()` and `setClockReadyCallback()`, then replace
`tick(networkReady, clockReady)` with `tick()`. Missing or cleared callbacks mean
unavailable. Outstanding results are still collected while waiting.

## Requirements

- **Platforms:** ESP32.
- **Dependencies:** ArduinoJson 7 or later. Fetching weather requires an OpenWeather API key, network access, and a valid system clock.

## Installation

Place the `MDS_Weather` folder in your Arduino sketchbook's `libraries` directory. Install the required board package and dependencies using the Arduino IDE.

## Example

Print the temperature and conditions for a location whenever new weather arrives. Replace the OpenWeather API key and coordinates with your own. Install **ArduinoJson 7 or later**, **MDS_WiFi**, and **WiFiManager 2.0.17 or later** along with **MDS_Weather**.

The example sketch uses MDS_WiFi to handle all of the cumbersome connection management and provisioning, and the ESP32's NTP service for time.
With no saved Wi-Fi credentials, join the `Weather <MAC>` access point (password `12345678`) and enter your network details in its portal. Weather requests wait for a connection and a valid clock, which is needed to verify HTTPS certificates. Updates run in the background every ten minutes (`intervalSeconds`).

```cpp
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

    // The sample is borrowed for this call; keep callbacks brief and do not re-enter Weather.
    weather.setUpdateCallback([](const MDS_Weather::Sample& sample) {
        // Print each accepted update without retaining the sample reference.
        Serial.print(sample.temperatureF, 1);
        Serial.print(" F, ");
        Serial.println(sample.description);
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

// Service connectivity and deliver accepted weather updates.
void loop() {
    wifi.tick();

    weather.tick();
}
```

## Documentation

Detailed design and API documentation: [MDS_Weather](https://home.marcspencer.com/mds-weather).

## License

Firmware and software: **MPL-2.0**. Documentation and website: **CC BY-SA 4.0**. Any hardware design files and Gerbers: **CERN-OHL-W-2.0**.

## Contributions

Contributions require a Developer Certificate of Origin (DCO) sign-off. Use `git commit -s` to add a `Signed-off-by` line to each contribution commit.

`setStatusCallback()` accepts `MDS_Weather::StatusCallback`, defined as
`std::function<void(const String&)>`, matching the Wi-Fi and time services.
Pass `nullptr` to clear the callback.

### Error reporting

`error()` returns `Error::None`, `InvalidOptions`, `AllocationFailed`,
`WorkerStartFailed`, `RequestFailed`, `ResponseTooLarge`, or `InvalidResponse`.
`lastError()` supplies diagnostic text; `httpStatus()` retains the exact HTTP
status or transport error from the last completed request.

Changed valid options and successful fetches clear errors. Reapplying unchanged
options, waiting for connectivity or time, registering callbacks and reading
accessors preserve request errors. Reapplying valid options after invalid
options restores the service and clears the configuration error. Cached samples
remain available after request failures; use `fresh()` to check their age.
Worker failures reach the public error state only when `tick()` collects the
result, and results from an older configuration are discarded.

### Readiness callbacks

Register `setNetworkReadyCallback()` before expecting network work to start.
Weather also requires `setClockReadyCallback()` to report a usable system UTC clock for TLS.
Callbacks return `bool` and run synchronously from `tick()`; keep them quick and
nonblocking. An unset callback returns false in effect. Pass `nullptr` to clear
one. Registration is independent of `begin(options)`: missing callbacks do not
invalidate options or overwrite errors. Call `tick()` without arguments.

Weather still collects outstanding results and serves cached samples while waiting.
Readiness status reflects the latest tick.

### Accepted sample counter

`sampleVersion()` starts at zero and increments once whenever `tick()` accepts a
successful result for the current configuration, before delivering `Weather: updated`.
Even identical samples or results sharing a UTC timestamp increment it. Failed
requests, discarded results, and configuration changes do not increment or reset
it. The `uint32_t` counter wraps naturally; compare versions with `!=` to detect
updates and use `available()` separately to check whether the cache is usable.

### Update callback

`setUpdateCallback()` accepts an optional `void(const Sample&)` callback, invoked
synchronously from `tick()` after committing each successful result and before the
status notification. Failures and discarded results do not invoke it; `nullptr`
clears it. The sample reference is borrowed for the callback's duration. Keep
callbacks brief and do not re-enter Weather. `sampleVersion()` remains available
for polling consumers.
