#include "whisper_pair_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const whisper_pair_scene_on_enter_handlers[])(void*) = {
#include "whisper_pair_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const whisper_pair_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "whisper_pair_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const whisper_pair_scene_on_exit_handlers[])(void*) = {
#include "whisper_pair_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers whisper_pair_scene_handlers = {
    .on_enter_handlers = whisper_pair_scene_on_enter_handlers,
    .on_event_handlers = whisper_pair_scene_on_event_handlers,
    .on_exit_handlers = whisper_pair_scene_on_exit_handlers,
    .scene_num = WhisperPairSceneNum,
};
