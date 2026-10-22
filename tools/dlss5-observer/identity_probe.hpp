#pragma once
#include <reshade.hpp>
#include "fsscenesignal.h"
namespace identity_probe {
void init(reshade::api::effect_runtime *runtime);
void destroy(reshade::api::effect_runtime *runtime);
void reload(reshade::api::effect_runtime *runtime);
void begin(reshade::api::effect_runtime *runtime);
void end(reshade::api::effect_runtime *runtime);
void technique(reshade::api::effect_runtime *runtime, reshade::api::effect_technique technique, bool present);
void finish_present(reshade::api::command_list *cmd);
void run(const FSSceneReadyV1 &packet, bool correlated, reshade::api::command_list *cmd);
}
