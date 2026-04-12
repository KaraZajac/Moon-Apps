#pragma once

typedef enum {
    FtCustomEventTick,
    FtCustomEventCocConnected,
    FtCustomEventCocDisconnected,
    FtCustomEventCocDataReceived,
    FtCustomEventCocTxDone,
    FtCustomEventCocCredits,
    FtCustomEventCocError,
    FtCustomEventFilePicked,
    FtCustomEventTransferComplete,
} FtCustomEvent;
