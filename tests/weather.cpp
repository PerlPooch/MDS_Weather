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
#include <cmath>
Stream Serial;uint32_t nowMillis=0;int enableLevel=0;bool failQueue=false,failTask=false,bundleUsed=false;
std::vector<PendingTask> tasks;HttpState httpState;
time_t fakeTime(time_t* value){time_t now=1792944000+nowMillis/1000;if(value)*value=now;return now;}
void good(){httpState.status=200;httpState.size=-1;httpState.beginOK=true;httpState.payload=R"({"main":{"temp":-12.5},"timezone":-14400,"dt":1792944000,"weather":[{"main":"Snow","description":"light snow"}]})";}
void finish(MDS_Weather& weather){runWorker();weather.tick(true,true);}
int main(){
 MDS_Weather weather;MDS_Weather::Options options;assert(weather.begin(options));weather.tick(true,true);assert(!weather.enabled()&&tasks.empty()&&httpState.calls==0);
 options.apiKey="TESTKEY";assert(weather.begin(options));weather.tick(true,true);assert(weather.enabled()&&!weather.configured()&&tasks.empty());
 options.latitude="42.5";options.longitude="-71.3";assert(weather.begin(options));weather.tick(false,true);weather.tick(true,false);assert(tasks.empty());
 good();weather.tick(true,true);assert(weather.fetching()&&tasks.size()==1&&httpState.calls==0);finish(weather);
 assert(weather.available()&&weather.fresh()&&bundleUsed&&weather.httpStatus()==200&&weather.lastSuccessUtc()==1792944000);
 assert(weather.sample().temperatureF==-12.5f&&weather.sample().timezoneOffsetSeconds==-14400&&String(weather.sample().condition)=="Snow");assert(httpState.url.find("https://api.openweathermap.org/data/2.5/weather?")==0&&httpState.url.find("units=imperial")!=String::npos);assert(weather.nextUpdateSeconds()==600);
 nowMillis=599999;weather.tick(true,true);assert(tasks.empty());nowMillis=600000;weather.tick(true,true);httpState.status=500;finish(weather);assert(weather.available()&&weather.nextUpdateSeconds()==60);assert(String(weather.lastError()).find("TESTKEY")==String::npos);
 nowMillis=660000;weather.tick(true,true);finish(weather);assert(weather.nextUpdateSeconds()==600);
 nowMillis=1260000;weather.tick(true,true);good();finish(weather);assert(weather.fresh()&&weather.lastSuccessUtc()>1792944000);
 auto previous=weather.lastSuccessUtc();nowMillis+=600000;weather.tick(true,true);httpState.payload="{}";finish(weather);assert(weather.available()&&weather.lastSuccessUtc()==previous);
 // Disabling during a request discards its result and starts no new work.
 nowMillis+=60000;weather.tick(true,true);assert(weather.fetching());options.apiKey="";assert(weather.begin(options));good();finish(weather);assert(!weather.enabled()&&!weather.available()&&tasks.empty());
 options.apiKey="TESTKEY";options.intervalSeconds=60;assert(weather.begin(options));weather.tick(true,true);httpState.status=429;finish(weather);assert(weather.nextUpdateSeconds()==600);
 // Reset timing around millis rollover; the due check must still wait 60 seconds.
 options.intervalSeconds=61;nowMillis=0xfffffff0U;assert(weather.begin(options));good();weather.tick(true,true);finish(weather);nowMillis+=60999;weather.tick(true,true);assert(tasks.empty());++nowMillis;weather.tick(true,true);finish(weather);
 // A location edit invalidates old samples/in-flight requests.
 nowMillis+=61000;weather.tick(true,true);options.latitude="0";options.longitude="0";assert(weather.begin(options));finish(weather);assert(!weather.available()&&tasks.size()==1);finish(weather);assert(weather.sample().latitude==0&&weather.sample().longitude==0);
 JsonDocument value;for(const char* invalid:{"91","nan"," 42","12junk"}){value["v"]=invalid;assert(!MDS_Weather::validLatitude(value["v"]));}
 value["v"]="-180";assert(MDS_Weather::validLongitude(value["v"]));value["v"]="";assert(MDS_Weather::validLatitude(value["v"])&&MDS_Weather::validApiKey(value["v"]));value["v"]="key&other=value";assert(!MDS_Weather::validApiKey(value["v"]));
 nowMillis+=61000;weather.tick(true,true);httpState.size=20000;finish(weather);assert(weather.available()&&String(weather.lastError()).find("large")!=String::npos);httpState.size=-1;
 nowMillis+=60000;weather.tick(true,true);httpState.payload=String("{\"main\":{\"temp\":70},\"timezone\":-2147483648,\"dt\":1792944000,\"weather\":[{\"main\":\"Clear\"}]}");finish(weather);assert(String(weather.lastError()).find("invalid")!=String::npos);
 {
  MDS_Weather failing;options.intervalSeconds=600;assert(failing.begin(options));failQueue=true;failing.tick(true,true);assert(!failing.fetching()&&tasks.empty());failQueue=false;nowMillis+=60000;failTask=true;failing.tick(true,true);assert(!failing.fetching()&&tasks.empty());failTask=false;
 }
 assert(tasks.empty());
 puts("PASS: disabled/missing-location gates, separate worker, verified HTTPS selection, cached parsing, retries/rate limit, rollover, stale-generation rejection, validators, and payload bounds.");
}
