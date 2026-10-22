#pragma once
#include <reshade.hpp>
#include "fsscenesignal.h"
namespace early_neural {
bool lean_mode();
uint32_t notify(const FSSceneReadyV1 *packet);
void begin_present(reshade::api::command_list *cmd);
void init(reshade::api::effect_runtime *rt);
void destroy(reshade::api::effect_runtime *rt);
void reload(reshade::api::effect_runtime *rt);
void technique(reshade::api::effect_runtime *rt, reshade::api::effect_technique tech, bool present);
void finish_present(reshade::api::command_list *cmd);
void run(const FSSceneReadyV1 &p, bool correlated, reshade::api::command_list *cmd);
}
