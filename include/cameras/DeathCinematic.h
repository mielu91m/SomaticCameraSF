/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "cameras/ICamera.h"
#include "cameras/Events.h"

namespace Camera {

    class DeathCinematic : public ICamera {

    public:
        DeathCinematic(Events* events);
        ~DeathCinematic() override = default;

        CameraType GetType() const override { return CameraType::kDeathCinematic; }
        std::string GetName() const override { return "DeathCinematic"; }

        bool OnEnter() override;
        bool OnExit() override;
        bool OnUpdate() override;

    private:
        Events* m_Events;
    };
}
