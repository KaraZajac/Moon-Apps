#pragma once

typedef enum {
    // Scan events
    BleConnectCustomEventScanComplete,
    BleConnectCustomEventScanUpdate,

    // Connection events
    BleConnectCustomEventConnected,
    BleConnectCustomEventConnectFailed,
    BleConnectCustomEventDisconnected,
    BleConnectCustomEventConnectTimeout,

    // Pairing events
    BleConnectCustomEventPinCodeShow,
    BleConnectCustomEventPinCodeVerify,
    BleConnectCustomEventPairingComplete,
    BleConnectCustomEventPairingFailed,

    // GATT events
    BleConnectCustomEventServicesDiscovered,
    BleConnectCustomEventCharsDiscovered,
    BleConnectCustomEventReadComplete,
    BleConnectCustomEventWriteComplete,
    BleConnectCustomEventNotification,
    BleConnectCustomEventGattError,
} BleConnectCustomEvent;
