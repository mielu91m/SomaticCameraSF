/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "cameras/ICamera.h"
#include "cameras/Events.h"

namespace Camera {

    class Ragdoll : public ICamera {

    public:
        Ragdoll(Events* events);
        ~Ragdoll() override = default;

        CameraType GetType() const override { return CameraType::kRagdoll; }
        std::string GetName() const override { return "Ragdoll"; }

        bool OnEnter() override;
        bool OnExit() override;
        bool OnUpdate() override;

    private:
        Events* m_Events;
    };
}
