# MDS_Weather

Background OpenWeather current conditions over verified HTTPS, with cached results and scheduled retries.

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

    MDS_Weather::Options options;
    
    options.apiKey = "YOURAPIKEY";
    options.latitude = "40.7128";
    options.longitude = "-74.0060";
    options.intervalSeconds = 600;

    if (!weather.begin(options)) {
        Serial.println(weather.lastError());
    }
}

void loop() {
    wifi.tick();

    const bool networkReady = wifi.ready();
    const bool clockReady = time(nullptr) > 1577836800;  // After Jan 1, 2020.
    
    weather.tick(networkReady, clockReady);

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
```

## Documentation

Detailed design and API documentation: [MDS_Weather](https://home.marcspencer.com/mds-weather).

## License

Firmware and software: **MPL-2.0**. Documentation and website: **CC BY-SA 4.0**. Any hardware design files and Gerbers: **CERN-OHL-W-2.0**.

## Contributions

Contributions require a Developer Certificate of Origin (DCO) sign-off. Use `git commit -s` to add a `Signed-off-by` line to each contribution commit.
