/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cameras/DeathCinematic.h"

namespace Camera {

    DeathCinematic::DeathCinematic(Events* events) :
        m_Events(events)
    {
        m_IsFirstPerson = false;
    }

    bool DeathCinematic::OnEnter()
    {
        m_Active = true;
        if (m_Events) {
            m_Events->Trigger(EventType::kOnEnterDeathCinematic);
        }
        spdlog::info("DeathCinematic camera entered");
        return true;
    }

    bool DeathCinematic::OnExit()
    {
        m_Active = false;
        if (m_Events) {
            m_Events->Trigger(EventType::kOnExitDeathCinematic);
        }
        spdlog::info("DeathCinematic camera exited");
        return true;
    }

    bool DeathCinematic::OnUpdate()
    {
        if (!m_Active) {
            return false;
        }

        if (m_Events) {
            m_Events->Trigger(EventType::kOnCameraUpdate);
        }

        return true;
    }
}
