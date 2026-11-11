/*
 * ===========================================================================
 * MDS_Weather 0.2.0 - https://home.marcspencer.com/mds-weather
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

#include "MDS_Weather.h"
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <new>

namespace {
	constexpr uint32_t RetrySeconds = 60;
	constexpr size_t MaximumPayload = 16384;

	// Bound even responses without Content-Length, while retaining Stream read timeouts.
	class LimitedReader {
	public:
		// Borrow the input stream and start with the maximum response budget.
		explicit LimitedReader(Stream& stream) : stream_(stream) {
		}

		// Read one byte within the response budget, or return -1 when none is available.
		int read() {
			char value;
			return readBytes(&value, 1) == 1 ? uint8_t(value) : -1;
		}

		// Read up to the requested length without exceeding the remaining response budget.
		size_t readBytes(char* buffer, size_t length) {
			const size_t count = length < remaining_ ? length : remaining_;
			const size_t received = stream_.readBytes(buffer, count);
			remaining_ -= received;
			return received;
		}

		// Report whether parsing consumed the entire bounded response budget.
		bool exhausted() const {
			return remaining_ == 0;
		}

	private:
		Stream& stream_;
		size_t remaining_ = MaximumPayload;
	};
}

// Wait for any outstanding worker result before deleting the result queue.
MDS_Weather::~MDS_Weather() {
	if (results_) {
		if (busy_) {
			Result result;
			xQueueReceive(results_, &result, portMAX_DELAY);
		}

		vQueueDelete(results_);
	}
}

// Parse an optional coordinate and enforce its inclusive geographic bounds.
bool MDS_Weather::coordinate(const char* text, double limit, double& number) {
	if (!text || !*text) {
		number = 0;
		return text != nullptr;
	}

	for (const char* p = text; *p; ++p) {
		if (isspace(static_cast<unsigned char>(*p))) {
			return false;
		}
	}

	char* end;
	number = strtod(text, &end);
	return end != text && !*end && isfinite(number) && number >= -limit && number <= limit;
}

// Accept an empty API key or up to 64 alphanumeric characters.
bool MDS_Weather::keyValid(const char* key) {
	if (!key || strlen(key) > 64) {
		return false;
	}

	for (const char* p = key; *p; ++p) {
		if (!isalnum(static_cast<unsigned char>(*p))) {
			return false;
		}
	}

	return true;
}

// Validate a JSON string as an optional alphanumeric API key.
bool MDS_Weather::validApiKey(JsonVariantConst value) {
	return value.is<const char*>() && keyValid(value.as<const char*>());
}

// Validate a JSON string as an optional latitude from -90 to 90 degrees.
bool MDS_Weather::validLatitude(JsonVariantConst value) {
	double number;
	return value.is<const char*>() && coordinate(value.as<const char*>(), 90, number);
}

// Validate a JSON string as an optional longitude from -180 to 180 degrees.
bool MDS_Weather::validLongitude(JsonVariantConst value) {
	double number;
	return value.is<const char*>() && coordinate(value.as<const char*>(), 180, number);
}

// Deliver a weather service message when an application callback is registered.
void MDS_Weather::status(const char* message) {
	if (statusCallback_) {
		statusCallback_(String(message));
	}
}

// Store a weather error and report it through the status callback.
void MDS_Weather::fail(Error error, const char* message) {
	error_ = error;
	strlcpy(errorText_, message, sizeof(errorText_));
	status(errorText_);
}

// Clear the failure code and diagnostic text after recovery or an options change.
void MDS_Weather::clearError() {
	error_ = Error::None;
	errorText_[0] = 0;
}

// Validate and copy weather options, disabling requests when the API key is empty.
bool MDS_Weather::begin(const Options& options) {
	Request next;

	if (!keyValid(options.apiKey) || !options.latitude || strlen(options.latitude) > 24 || !options.longitude
		|| strlen(options.longitude) > 24 || !coordinate(options.latitude, 90, next.latitude)
		|| !coordinate(options.longitude, 180, next.longitude) || options.intervalSeconds < 60
		|| options.intervalSeconds > 86400) {
		enabled_ = false;
		configured_ = false;
		available_ = false;
		++generation_;
		fail(Error::InvalidOptions, "Weather: invalid options");
		return false;
	}

	strlcpy(next.key, options.apiKey, sizeof(next.key));
	next.hasLocation = *options.latitude && *options.longitude;
	next.intervalSeconds = options.intervalSeconds;

	// Invalid options disable the service, so reapplying the previous valid options must restore it.
	if (initialized_ && error_ != Error::InvalidOptions && strcmp(next.key, request_.key) == 0
		&& next.latitude == request_.latitude && next.longitude == request_.longitude
		&& next.hasLocation == request_.hasLocation && next.intervalSeconds == request_.intervalSeconds) {
		return true;
	}

	const bool changedTarget = !initialized_ || strcmp(next.key, request_.key) != 0
							   || next.latitude != request_.latitude || next.longitude != request_.longitude
							   || next.hasLocation != request_.hasLocation;
	request_ = next;
	initialized_ = true;
	enabled_ = *next.key;
	configured_ = enabled_ && next.hasLocation;
	++generation_;
	nextDue_ = millis();
	failures_ = 0;
	clearError();
	httpStatus_ = 0;

	if (changedTarget) {
		available_ = false;
		lastSuccessUtc_ = 0;
		sample_ = Sample();
	}

	status(!enabled_ ? "Weather: disabled (no key)"
					 : (!configured_ ? "Weather: waiting for location" : "Weather: configured"));
	return true;
}

// Return seconds since the latest successful fetch, or zero when no sample exists.
uint32_t MDS_Weather::ageSeconds() const {
	if (!available_) {
		return 0;
	}

	const time_t elapsed = time(nullptr) - lastSuccessUtc_;

	if (clockReady_ && elapsed >= 0 && uint64_t(elapsed) <= UINT32_MAX) {
		return uint32_t(elapsed);
	}

	return uint32_t(millis() - lastSuccessMillis_) / 1000;
}

// Report whether cached conditions are at most two update intervals old.
bool MDS_Weather::fresh() const {
	return available_ && ageSeconds() <= request_.intervalSeconds * 2;
}

// Return seconds until the next request; zero means due, inactive, or already fetching.
uint32_t MDS_Weather::nextUpdateSeconds() const {
	const int32_t remaining = int32_t(nextDue_ - millis());
	return configured_ && !busy_ && remaining > 0 ? (uint32_t(remaining) + 999) / 1000 : 0;
}

// Collect worker results and schedule requests when network, clock, and timing allow.
void MDS_Weather::tick() {
	const bool networkReady = networkReadyCallback_ && networkReadyCallback_();
	const bool clockReady = clockReadyCallback_ && clockReadyCallback_();

	networkReady_ = networkReady;
	clockReady_ = clockReady;
	Result result;

	if (busy_ && xQueueReceive(results_, &result, 0) == pdTRUE) {
		busy_ = false;

		if (result.generation == generation_) {
			httpStatus_ = result.httpStatus;

			if (result.success) {
				sample_ = result.sample;
				available_ = true;
				lastSuccessMillis_ = millis();
				lastSuccessUtc_ = time(nullptr);
				failures_ = 0;
				clearError();
				nextDue_ = millis() + request_.intervalSeconds * 1000UL;
				status("Weather: updated");
			} else {
				fail(result.error, result.errorText);
				++failures_;
				const bool wait = failures_ >= 2 || result.httpStatus == 401 || result.httpStatus == 403
								  || result.httpStatus == 429;
				uint32_t seconds = wait ? request_.intervalSeconds : RetrySeconds;

				if (result.httpStatus == 429 && seconds < 600) {
					seconds = 600;
				}

				nextDue_ = millis() + seconds * 1000UL;

				if (wait) {
					failures_ = 0;
				}
			}
		}
	}

	if (!configured_ || busy_ || !networkReady || !clockReady || int32_t(millis() - nextDue_) < 0) {
		return;
	}

	if (!results_) {
		results_ = xQueueCreate(1, sizeof(Result));
	}

	if (!results_) {
		fail(Error::AllocationFailed, "Weather: cannot allocate result queue");
		nextDue_ = millis() + RetrySeconds * 1000UL;
		return;
	}

	Job* job = new (std::nothrow) Job{request_, results_, generation_};

	if (!job) {
		fail(Error::AllocationFailed, "Weather: cannot allocate request");
		nextDue_ = millis() + RetrySeconds * 1000UL;
		return;
	}

	if (xTaskCreate(worker, "MDS_Weather", 12288, job, 1, nullptr) != pdPASS) {
		memset(job->request.key, 0, sizeof(job->request.key));
		delete job;
		fail(Error::WorkerStartFailed, "Weather: cannot start worker");
		nextDue_ = millis() + RetrySeconds * 1000UL;
		return;
	}

	busy_ = true;
}

// Fetch one weather result, queue it, erase the copied key, and exit the worker task.
void MDS_Weather::worker(void* argument) {
	Job* job = static_cast<Job*>(argument);
	const Result result = fetch(*job);
	xQueueSend(job->results, &result, portMAX_DELAY);
	memset(job->request.key, 0, sizeof(job->request.key));
	delete job;
	vTaskDelete(nullptr);
}

// Request and validate bounded HTTPS weather data without changing application state.
MDS_Weather::Result MDS_Weather::fetch(const Job& job) {
	Result result;
	result.generation = job.generation;
	NetworkClientSecure client;
	client.useBuiltinCACertBundle();
	client.setHandshakeTimeout(5);
	HTTPClient http;
	http.setConnectTimeout(5000);
	http.setTimeout(5000);
	http.useHTTP10(true);
	const String url = String("https://api.openweathermap.org/data/2.5/weather?lat=")
					   + String(job.request.latitude, 6) + "&lon=" + String(job.request.longitude, 6)
					   + "&units=imperial&appid=" + job.request.key;

	if (!http.begin(client, url)) {
		result.error = Error::RequestFailed;
		strlcpy(result.errorText, "Weather: HTTPS initialization failed", sizeof(result.errorText));
		return result;
	}

	result.httpStatus = http.GET();

	if (result.httpStatus != 200) {
		result.error = Error::RequestFailed;
		snprintf(result.errorText, sizeof(result.errorText), "Weather: HTTP %d", result.httpStatus);
		http.end();
		return result;
	}

	if (http.getSize() > int(MaximumPayload)) {
		result.error = Error::ResponseTooLarge;
		strlcpy(result.errorText, "Weather: response too large", sizeof(result.errorText));
		http.end();
		return result;
	}

	JsonDocument filter;
	filter["main"]["temp"] = true;
	filter["timezone"] = true;
	filter["dt"] = true;
	filter["weather"][0]["main"] = true;
	filter["weather"][0]["description"] = true;

	if (filter.overflowed()) {
		result.error = Error::AllocationFailed;
		strlcpy(result.errorText, "Weather: cannot allocate response filter", sizeof(result.errorText));
		http.end();
		return result;
	}

	JsonDocument response;
	LimitedReader input(http.getStream());
	const auto parsed = deserializeJson(response, input, DeserializationOption::Filter(filter));
	http.end();

	if (parsed == DeserializationError::NoMemory) {
		result.error = Error::AllocationFailed;
		strlcpy(result.errorText, "Weather: cannot allocate response", sizeof(result.errorText));
		return result;
	}

	if (parsed && input.exhausted()) {
		result.error = Error::ResponseTooLarge;
		strlcpy(result.errorText, "Weather: response too large", sizeof(result.errorText));
		return result;
	}

	if (parsed || !response["main"]["temp"].is<float>() || !response["timezone"].is<int32_t>()
		|| !response["dt"].is<uint32_t>() || !response["weather"][0]["main"].is<const char*>()) {
		result.error = Error::InvalidResponse;
		strlcpy(result.errorText, "Weather: invalid response", sizeof(result.errorText));
		return result;
	}

	result.sample.temperatureF = response["main"]["temp"].as<float>();

	if (!isfinite(result.sample.temperatureF) || result.sample.temperatureF < -200
		|| result.sample.temperatureF > 200 || response["timezone"].as<int32_t>() < -86400
		|| response["timezone"].as<int32_t>() > 86400) {
		result.error = Error::InvalidResponse;
		strlcpy(result.errorText, "Weather: invalid response values", sizeof(result.errorText));
		return result;
	}

	result.sample.latitude = job.request.latitude;
	result.sample.longitude = job.request.longitude;
	result.sample.timezoneOffsetSeconds = response["timezone"].as<int32_t>();
	result.sample.observedUtc = response["dt"].as<uint32_t>();
	strlcpy(result.sample.condition, response["weather"][0]["main"].as<const char*>(),
		sizeof(result.sample.condition));
	strlcpy(result.sample.description, response["weather"][0]["description"] | "",
		sizeof(result.sample.description));
	result.success = true;
	return result;
}
