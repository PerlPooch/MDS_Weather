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
#include <new>

static bool networkReady = true;
static bool clockReady = true;

using Error = MDS_Weather::Error;
Stream Serial;
uint32_t nowMillis = 0;
int enableLevel = 0;
bool failQueue = false;
bool failTask = false;
bool bundleUsed = false;
static bool failJobAllocation = false;
std::vector<PendingTask> tasks;
HttpState httpState;

// Fail only nothrow allocation, used when creating the weather request job.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
	if (failJobAllocation) {
		failJobAllocation = false;
		return nullptr;
	}

	try {
		return ::operator new(size);
	} catch (...) {
		return nullptr;
	}
}

// Release a job if its constructor fails after nothrow allocation.
void operator delete(void* pointer, const std::nothrow_t&) noexcept {
	::operator delete(pointer);
}

// Keep UTC deterministic while simulated milliseconds advance.
time_t fakeTime(time_t* value) {
	const time_t now = 1792944000 + nowMillis / 1000;
	if (value) {
		*value = now;
	}

	return now;
}

// Supply a valid response while resetting earlier injected HTTP faults.
static void goodResponse() {
	httpState.beginOK = true;
	httpState.status = 200;
	httpState.size = -1;
	httpState.payload = R"({"main":{"temp":70},"timezone":0,"dt":1792944000,"weather":[{"main":"Clear"}]})";
}

// Advance past retry delays and process one complete request on the main thread.
static void fetch(MDS_Weather& weather) {
	nowMillis += 86400000;
	weather.tick();
	assert(weather.fetching());
	runWorker();
	weather.tick();
	assert(!weather.fetching());
}

// Cover failure classification, worker transfer, persistence and configuration recovery.
int main() {
	MDS_Weather weather;
	weather.setNetworkReadyCallback([]() {
		// Expose the connectivity selected by each scenario.
		return networkReady;
	});
	weather.setClockReadyCallback([]() {
		// Expose the clock validity selected by each scenario.
		return clockReady;
	});

	MDS_Weather::Options options{"TESTKEY", "42.5", "-71.3", 600};
	std::vector<String> messages;
	weather.setStatusCallback([&](const String& message) {
		// Capture diagnostics to verify they never contain the API key.
		messages.push_back(message);
	});
	assert(weather.error() == Error::None);
	assert(String(weather.lastError()).empty());
	assert(weather.begin(options));
	networkReady = false;
	weather.tick();
	networkReady = true;

	clockReady = false;
	weather.tick();
	clockReady = true;

	assert(weather.error() == Error::None);

	// Returning to the previous valid options must restore a disabled service.
	auto invalid = options;
	invalid.latitude = "91";
	assert(!weather.begin(invalid));
	assert(weather.error() == Error::InvalidOptions);
	assert(!weather.configured());
	assert(weather.begin(options));
	assert(weather.configured() && weather.error() == Error::None);

	failQueue = true;
	weather.tick();
	assert(weather.error() == Error::AllocationFailed);
	assert(!weather.fetching());
	failQueue = false;
	nowMillis += 60000;
	failJobAllocation = true;
	weather.tick();
	assert(weather.error() == Error::AllocationFailed);
	assert(String(weather.lastError()) == "Weather: cannot allocate request");
	nowMillis += 60000;
	failTask = true;
	weather.tick();
	assert(weather.error() == Error::WorkerStartFailed);
	assert(tasks.empty());
	failTask = false;
	goodResponse();
	fetch(weather);
	assert(weather.error() == Error::None && weather.available());

	httpState.beginOK = false;
	fetch(weather);
	assert(weather.error() == Error::RequestFailed);
	assert(weather.httpStatus() == 0);
	goodResponse();
	httpState.status = -1;
	fetch(weather);
	assert(weather.error() == Error::RequestFailed && weather.httpStatus() == -1);
	httpState.status = 401;
	fetch(weather);
	assert(weather.error() == Error::RequestFailed && weather.httpStatus() == 401);

	// Reapplying configuration, waiting and accessors must preserve request errors.
	assert(weather.begin(options));
	assert(weather.error() == Error::RequestFailed);
	networkReady = false;
	clockReady = false;
	weather.tick();
	networkReady = true;
	clockReady = true;
	(void)weather.state();
	(void)weather.sample();
	(void)weather.fresh();
	(void)weather.ageSeconds();
	(void)weather.nextUpdateSeconds();
	(void)weather.lastSuccessUtc();
	weather.setStatusCallback(nullptr);
	assert(weather.error() == Error::RequestFailed);
	assert(weather.available());

	goodResponse();
	httpState.size = 20000;
	fetch(weather);
	assert(weather.error() == Error::ResponseTooLarge);
	httpState.size = -1;
	httpState.payload = String(17000, ' ') + "{}";
	fetch(weather);
	assert(weather.error() == Error::ResponseTooLarge);
	httpState.payload = "{}";
	fetch(weather);
	assert(weather.error() == Error::InvalidResponse);
	httpState.payload = "{";
	fetch(weather);
	assert(weather.error() == Error::InvalidResponse);
	goodResponse();
	httpState.payload = R"({"main":{"temp":999},"timezone":0,"dt":1792944000,"weather":[{"main":"Clear"}]})";
	fetch(weather);
	assert(weather.error() == Error::InvalidResponse);

	goodResponse();
	fetch(weather);
	assert(weather.error() == Error::None);
	assert(String(weather.lastError()).empty());
	assert(weather.httpStatus() == 200);
	assert(weather.available() && weather.fresh());

	// Configuration changes clear failures and invalidate stale worker results.
	httpState.status = 500;
	fetch(weather);
	++options.intervalSeconds;
	assert(weather.begin(options));
	assert(weather.error() == Error::None);
	weather.tick();
	assert(weather.fetching());
	options.apiKey = "";
	assert(weather.begin(options));
	runWorker();
	weather.tick();
	assert(weather.error() == Error::None);
	assert(!weather.enabled() && !weather.available());
	assert(tasks.empty());

	for (const auto& message : messages) {
		assert(message.find("TESTKEY") == String::npos);
	}

	puts("PASS: Weather error codes, allocation/worker faults, bounded responses and recovery.");
}
