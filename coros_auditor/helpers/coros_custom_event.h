#pragma once

typedef enum {
    CorosCustomEventTick,
    CorosCustomEventServicesDiscovered,
    CorosCustomEventCharsDiscovered,
    CorosCustomEventWriteComplete,
    CorosCustomEventReadComplete,
    CorosCustomEventNotification,
    CorosCustomEventGattError,
} CorosCustomEvent;
