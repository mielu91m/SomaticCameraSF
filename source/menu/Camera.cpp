/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "menu/Camera.h"
#include "plugin.h"
#include "menu/UIMenuHelper.h"

namespace Menu {

    void Camera::Draw()
    {
        auto config = DLLMain::Plugin::Get()->GetConfig();
        if (!config) return;

        auto cam = config->Camera();

        ImGui::Text("Camera Settings");
        ImGui::Separator();

        ImGui::Checkbox("Enable Camera Overrides", &cam->enableCameraOverrides);
        ImGui::SliderFloat("Translation Smoothing", &cam->translationSmoothing, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Rotation Smoothing", &cam->rotationSmoothing, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Hide Weapon In Menu", &cam->hideWeaponInMenu);
        ImGui::Checkbox("Disable Auto Vanity", &cam->disableAutoVanity);
    }
}
