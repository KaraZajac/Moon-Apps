#include "meshtastic_ble.h"
#include <string.h>

bool meshtastic_is_mesh_service(const uint8_t* uuid128) {
    return memcmp(uuid128, MESH_SERVICE_UUID, 16) == 0;
}
