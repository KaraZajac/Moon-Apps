#pragma once

typedef enum {
    LockTesterCustomEventTick,
    LockTesterCustomEventServicesDiscovered,
    LockTesterCustomEventCharsDiscovered,
    LockTesterCustomEventWriteComplete,
    LockTesterCustomEventReadComplete,
    LockTesterCustomEventNotification,
    LockTesterCustomEventGattError,
} LockTesterCustomEvent;
