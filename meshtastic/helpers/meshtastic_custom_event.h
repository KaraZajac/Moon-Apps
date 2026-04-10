#pragma once

/* Start at 0x100 to avoid colliding with submenu indices (0-0xFF) */
typedef enum {
    // Scan/Connect
    MeshtasticCustomEventTimerTick = 0x100,
    MeshtasticCustomEventConnected,
    MeshtasticCustomEventConnectFailed,
    MeshtasticCustomEventDisconnected,

    // Config reception
    MeshtasticCustomEventConfigReceived,
    MeshtasticCustomEventConfigComplete,

    // Messages
    MeshtasticCustomEventMessageReceived,
    MeshtasticCustomEventMessageSent,
    MeshtasticCustomEventNodeUpdated,

    // GATT
    MeshtasticCustomEventFromRadioReady,
    MeshtasticCustomEventWriteComplete,
    MeshtasticCustomEventGattError,
} MeshtasticCustomEvent;
