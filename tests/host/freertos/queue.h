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
#include "FreeRTOS.h"
#include <vector>
#include <cstring>
#include <cassert>
struct Queue {size_t size;std::vector<uint8_t> bytes;bool full=false;};
using QueueHandle_t=Queue*;
extern bool failQueue;
inline QueueHandle_t xQueueCreate(int,size_t size){if(failQueue)return nullptr;return new Queue{size,{},false};}
inline int xQueueSend(QueueHandle_t q,const void* source,uint32_t){assert(q&&!q->full);q->bytes.resize(q->size);memcpy(q->bytes.data(),source,q->size);q->full=true;return pdTRUE;}
inline int xQueueReceive(QueueHandle_t q,void* target,uint32_t){if(!q||!q->full)return pdFALSE;memcpy(target,q->bytes.data(),q->size);q->full=false;return pdTRUE;}
inline void vQueueDelete(QueueHandle_t q){delete q;}
