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
#include "FreeRTOS.h"
#include <vector>
#include <cassert>
struct PendingTask {void(*function)(void*);void* argument;};
extern std::vector<PendingTask> tasks;
extern bool failTask;
inline int xTaskCreate(void(*fn)(void*),const char*,uint32_t,void* arg,int,void*){if(failTask)return 0;tasks.push_back({fn,arg});return pdPASS;}
inline void vTaskDelete(void*){}
inline void runWorker(){assert(!tasks.empty());auto task=tasks.front();tasks.erase(tasks.begin());task.function(task.argument);}
