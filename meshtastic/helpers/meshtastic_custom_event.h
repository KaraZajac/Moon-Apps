#pragma once

typedef enum {
    // Scan/Connect
    MeshtasticCustomEventTimerTick,
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
