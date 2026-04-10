#pragma once

typedef enum {
    BitchatCustomEventTick,
    BitchatCustomEventServicesDiscovered,
    BitchatCustomEventCharsDiscovered,
    BitchatCustomEventWriteComplete,
    BitchatCustomEventNotification,
    BitchatCustomEventGattError,
    BitchatCustomEventMsgSend,
} BitchatCustomEvent;
