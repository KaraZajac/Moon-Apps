#pragma once

typedef enum {
    EcovacsCustomEventTick,
    EcovacsCustomEventServicesDiscovered,
    EcovacsCustomEventCharsDiscovered,
    EcovacsCustomEventWriteComplete,
    EcovacsCustomEventNotification,
    EcovacsCustomEventGattError,
} EcovacsCustomEvent;
