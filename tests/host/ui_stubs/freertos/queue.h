// SPDX-License-Identifier: MIT
#pragma once
#include "FreeRTOS.h"
using QueueHandle_t = void *;
int xQueueSend(QueueHandle_t, const void *, TickType_t);
int xQueueReceive(QueueHandle_t, void *, TickType_t);
int xQueueOverwrite(QueueHandle_t, const void *);

QueueHandle_t xQueueCreate(unsigned, unsigned);
void vQueueDelete(QueueHandle_t);
