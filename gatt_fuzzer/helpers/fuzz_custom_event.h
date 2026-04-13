#pragma once

typedef enum {
    FuzzCustomEventTick,
    FuzzCustomEventServicesDiscovered,
    FuzzCustomEventCharsDiscovered,
    FuzzCustomEventWriteComplete,
    FuzzCustomEventReadComplete,
    FuzzCustomEventNotification,
    FuzzCustomEventGattError,
} FuzzCustomEvent;
