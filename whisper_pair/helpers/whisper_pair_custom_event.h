#pragma once

typedef enum {
    WpCustomEventTick,
    WpCustomEventServicesDiscovered,
    WpCustomEventCharsDiscovered,
    WpCustomEventWriteComplete,
    WpCustomEventNotification,
    WpCustomEventGattError,
    WpCustomEventTestTimeout,
} WpCustomEvent;
