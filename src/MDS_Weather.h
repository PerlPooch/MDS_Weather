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

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <time.h>
#include <functional>

// Optional OpenWeather current conditions; the application owns connectivity/config/time.
class MDS_Weather {
public:
	struct Options {
		const char* apiKey = "";
		const char* latitude = "";
		const char* longitude = "";
		uint32_t intervalSeconds = 600;
	};

	struct Sample {
		float temperatureF = 0;
		double latitude = 0;
		double longitude = 0;
		int32_t timezoneOffsetSeconds = 0;
		uint32_t observedUtc = 0;
		char condition[32] = {};
		char description[96] = {};
	};

	MDS_Weather() = default;
	~MDS_Weather();
	MDS_Weather(const MDS_Weather&) = delete;
	MDS_Weather& operator=(const MDS_Weather&) = delete;

	// Copy options; an empty key disables requests and clears the cached sample.
	bool begin(const Options& options);

	// Collect results and schedule a worker only when network and TLS clock are ready.
	void tick(bool networkReady, bool clockReady);
	bool enabled() const { return enabled_; }
	bool configured() const { return configured_; }
	bool fetching() const { return busy_; }
	bool available() const { return available_; }
	bool fresh() const;
	const char* state() const {
		if (!enabled_) {
			return "disabled";
		}
		if (!configured_) {
			return "waiting_location";
		}
		if (busy_) {
			return "fetching";
		}
		if (!networkReady_) {
			return "waiting_network";
		}
		if (!clockReady_) {
			return "waiting_time";
		}
		if (*error_) {
			return "error";
		}
		return available_ ? "ready" : "waiting_update";
	}
	uint32_t ageSeconds() const;
	uint32_t nextUpdateSeconds() const;
	time_t lastSuccessUtc() const { return lastSuccessUtc_; }
	int httpStatus() const { return httpStatus_; }
	const Sample& sample() const { return sample_; }
	const char* lastError() const { return error_; }
	void setStatusCallback(std::function<void(const String&)> callback) { statusCallback_ = callback; }

	// Optional registry validators; missing coordinates/key are allowed during setup.
	static bool validApiKey(JsonVariantConst value);
	static bool validLatitude(JsonVariantConst value);
	static bool validLongitude(JsonVariantConst value);

private:
	struct Request {
		char key[65] = {};
		double latitude = 0;
		double longitude = 0;
		uint32_t intervalSeconds = 600;
		bool hasLocation = false;
	};

	struct Result {
		uint32_t generation = 0;
		Sample sample;
		int httpStatus = 0;
		bool success = false;
		char error[96] = {};
	};

	struct Job {
		Request request;
		QueueHandle_t results;
		uint32_t generation;
	};

	static bool coordinate(const char* text, double limit, double& number);
	static bool keyValid(const char* key);
	static void worker(void* argument);
	static Result fetch(const Job& job);
	void status(const char* message);
	void error(const char* message);

	Request request_;
	Sample sample_;
	QueueHandle_t results_ = nullptr;
	std::function<void(const String&)> statusCallback_;
	char error_[96] = {};
	uint32_t generation_ = 0;
	uint32_t nextDue_ = 0;
	uint32_t lastSuccessMillis_ = 0;
	time_t lastSuccessUtc_ = 0;
	int httpStatus_ = 0;
	uint8_t failures_ = 0;
	bool initialized_ = false;
	bool enabled_ = false;
	bool configured_ = false;
	bool busy_ = false;
	bool available_ = false;
	bool networkReady_ = false;
	bool clockReady_ = false;
};
