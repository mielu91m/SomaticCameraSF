/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cameras/FirstPerson.h"
#include "starfield/Helper.h"

namespace Camera {

    FirstPerson::FirstPerson(Events* events) :
        m_Events(events)
    {
        m_IsFirstPerson = true;
    }

    bool FirstPerson::OnEnter()
    {
        m_Active = true;
        if (m_Events) {
            m_Events->Trigger(EventType::kOnEnterFirstPerson);
        }
        spdlog::info("FirstPerson camera entered");
        return true;
    }

    bool FirstPerson::OnExit()
    {
        m_Active = false;
        if (m_Events) {
            m_Events->Trigger(EventType::kOnExitFirstPerson);
        }
        spdlog::info("FirstPerson camera exited");
        return true;
    }

    bool FirstPerson::OnUpdate()
    {
        if (!m_Active) {
            return false;
        }

        auto playerCamera = RE::PlayerCamera::GetSingleton();
        if (!playerCamera) {
            return false;
        }

        if (m_Events) {
            m_Events->Trigger(EventType::kOnCameraUpdate);
        }

        return true;
    }
}
