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
#include "Arduino.h"
#include "NetworkClientSecure.h"
#include <algorithm>
struct HttpState {int calls=0,status=200,size=-1;bool beginOK=true;String payload,url;};
extern HttpState httpState;
class BodyStream : public Stream {
 size_t offset=0;
public:
 size_t readBytes(char* buffer,size_t count) override {size_t copied=std::min(count,httpState.payload.size()-offset);memcpy(buffer,httpState.payload.data()+offset,copied);offset+=copied;return copied;}
};
class HTTPClient {
 BodyStream body;
public:
 void setConnectTimeout(int){}
 void setTimeout(int){}
 void useHTTP10(bool){}
 bool begin(NetworkClientSecure&,const String& url){httpState.url=url;return httpState.beginOK;}
 int GET(){++httpState.calls;return httpState.status;}
 int getSize(){return httpState.size;}
 Stream& getStream(){return body;}
 void end(){}
};
