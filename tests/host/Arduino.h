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

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sstream>
#include <iomanip>
class String : public std::string {
public:
 using std::string::string;
 using std::string::operator=;
 String()=default;
 String(const std::string& text):std::string(text){}
 String(double value,int precision){std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;assign(out.str());}
 size_t write(uint8_t value){push_back(char(value));return 1;}
 size_t write(const uint8_t* values,size_t count){append(reinterpret_cast<const char*>(values),count);return count;}
};
inline size_t strlcpy(char* dest,const char* src,size_t count){size_t length=strlen(src);if(count){size_t copied=length<count-1?length:count-1;memcpy(dest,src,copied);dest[copied]=0;}return length;}
class Stream {
public:
 virtual ~Stream()=default;
 virtual size_t readBytes(char*,size_t){return 0;}
 virtual size_t write(uint8_t){return 1;}
 size_t write(const uint8_t* data,size_t count){for(size_t i=0;i<count;++i)write(data[i]);return count;}
 void print(const char* text){while(*text)write(uint8_t(*text++));}
 void println(){write(uint8_t('\n'));}
 void println(const char* text){print(text);println();}
};
extern Stream Serial;
extern uint32_t nowMillis;
inline uint32_t millis(){return nowMillis;}
extern time_t fakeTime(time_t*);
#define time fakeTime
constexpr int LOW=0,HIGH=1,OUTPUT=1;
extern int enableLevel;
inline void digitalWrite(uint8_t,int value){enableLevel=value;}
inline void pinMode(uint8_t,int){}
inline void delay(int){}
