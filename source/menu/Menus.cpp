/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "menu/Menus.h"
#include "plugin.h"

namespace Menu {

    void Menus::Draw()
    {
        ImGui::Text("Menu Toggles");
        ImGui::Separator();

        if (ImGui::Button("Open Full Editor")) {
        }
    }
}
