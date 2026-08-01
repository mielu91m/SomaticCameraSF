/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "menu/EventsMenu.h"
#include "plugin.h"
#include "menu/UIMenuHelper.h"

namespace Menu {

    void EventsMenu::Draw()
    {
        auto config = DLLMain::Plugin::Get()->GetConfig();
        if (!config) return;

        auto events = config->Events();

        ImGui::Text("Event Override Settings");
        ImGui::Separator();

        ImGui::Checkbox("Enable Event Overrides", &events->enableOverrideEvents);

        ImGui::Separator();

        for (auto& [eventName, state] : events->eventStates) {
            ImGui::Checkbox(eventName.c_str(), &state);
        }
    }
}
