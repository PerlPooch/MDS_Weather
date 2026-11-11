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
	enum class Error {
		None,
		InvalidOptions,
		AllocationFailed,
		WorkerStartFailed,
		RequestFailed,
		ResponseTooLarge,
		InvalidResponse
	};

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

	// Receive weather service status and error messages.
	using StatusCallback = std::function<void(const String&)>;

	// Query a prerequisite without taking ownership of its service.
	using ReadinessCallback = std::function<bool()>;

	// Create an inactive weather service; begin() supplies its options.
	MDS_Weather() = default;

	// Wait for any outstanding worker result before deleting the result queue.
	~MDS_Weather();

	// Prevent copying ownership of weather workers and their result queue.
	MDS_Weather(const MDS_Weather&) = delete;

	// Prevent assignment of an object that owns worker and queue state.
	MDS_Weather& operator=(const MDS_Weather&) = delete;

	// Copy options; an empty key disables requests and clears the cached sample.
	bool begin(const Options& options);

	// Collect results and schedule a worker when both readiness callbacks return true.
	void tick();

	// Report whether a nonempty API key enables weather requests.
	bool enabled() const {
		return enabled_;
	}

	// Report whether an API key and both coordinates are available for requests.
	bool configured() const {
		return configured_;
	}

	// Report whether a weather worker is currently outstanding.
	bool fetching() const {
		return busy_;
	}

	// Report whether cached conditions exist, regardless of their age.
	bool available() const {
		return available_;
	}

	// Report whether cached conditions are at most two update intervals old.
	bool fresh() const;

	// Return a status label describing the current request, waiting, or cache state.
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
		if (error_ != Error::None) {
			return "error";
		}
		return available_ ? "ready" : "waiting_update";
	}

	// Return seconds since the latest successful fetch, or zero when no sample exists.
	uint32_t ageSeconds() const;

	// Return seconds until the next request; zero means due, inactive, or already fetching.
	uint32_t nextUpdateSeconds() const;

	// Return the system UTC time of the latest successful fetch, or zero before one succeeds.
	time_t lastSuccessUtc() const {
		return lastSuccessUtc_;
	}

	// Return the last recorded HTTP status, or zero when no response status is stored.
	int httpStatus() const {
		return httpStatus_;
	}

	// Borrow cached conditions; check available() before using their values.
	const Sample& sample() const {
		return sample_;
	}

	// Return the latest failure code without changing request or cache state.
	Error error() const {
		return error_;
	}

	// Return the latest weather error, or an empty string when the error has been cleared.
	const char* lastError() const {
		return errorText_;
	}

	// Query connectivity from tick(); an unset callback means unavailable.
	// Keep callbacks nonblocking; nullptr clears the registered source.
	void setNetworkReadyCallback(ReadinessCallback callback) {
		networkReadyCallback_ = callback;
	}

	// Query TLS clock readiness from tick(); an unset callback means unavailable.
	// Keep callbacks nonblocking; nullptr clears the registered source.
	void setClockReadyCallback(ReadinessCallback callback) {
		clockReadyCallback_ = callback;
	}

	// Register the callback for weather status/error messages; nullptr clears it.
	void setStatusCallback(StatusCallback callback) {
		statusCallback_ = callback;
	}

	// Validate a JSON string as an optional alphanumeric API key.
	static bool validApiKey(JsonVariantConst value);

	// Validate a JSON string as an optional latitude from -90 to 90 degrees.
	static bool validLatitude(JsonVariantConst value);

	// Validate a JSON string as an optional longitude from -180 to 180 degrees.
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
		Error error = Error::None;
		char errorText[96] = {};
	};

	struct Job {
		Request request;
		QueueHandle_t results;
		uint32_t generation;
	};

	// Parse an optional coordinate and enforce its inclusive geographic bounds.
	static bool coordinate(const char* text, double limit, double& number);

	// Accept an empty API key or up to 64 alphanumeric characters.
	static bool keyValid(const char* key);

	// Fetch one weather result, queue it, erase the copied key, and exit the worker task.
	static void worker(void* argument);

	// Request and validate bounded HTTPS weather data without changing application state.
	static Result fetch(const Job& job);

	// Deliver a weather service message when an application callback is registered.
	void status(const char* message);

	// Store a weather error and report it through the status callback.
	void fail(Error error, const char* message);

	// Clear the failure code and diagnostic text after recovery or an options change.
	void clearError();

	Request request_;
	Sample sample_;
	QueueHandle_t results_ = nullptr;
	StatusCallback statusCallback_;
	ReadinessCallback networkReadyCallback_;
	ReadinessCallback clockReadyCallback_;
	Error error_ = Error::None;
	char errorText_[96] = {};
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
