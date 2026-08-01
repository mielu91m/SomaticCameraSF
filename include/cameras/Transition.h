/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "cameras/ICamera.h"
#include "cameras/Events.h"

namespace Camera {

    class Transition : public ICamera {

    public:
        Transition(Events* events);
        ~Transition() override = default;

        CameraType GetType() const override { return CameraType::kTransition; }
        std::string GetName() const override { return "Transition"; }

        bool OnEnter() override;
        bool OnExit() override;
        bool OnUpdate() override;

    private:
        Events* m_Events;
    };
}
