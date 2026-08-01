/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cameras/ThirdPerson.h"
#include "starfield/Helper.h"

namespace Camera {

    ThirdPerson::ThirdPerson(Events* events) :
        m_Events(events)
    {
        m_IsFirstPerson = false;
    }

    bool ThirdPerson::OnEnter()
    {
        m_Active = true;
        if (m_Events) {
            m_Events->Trigger(EventType::kOnEnterThirdPerson);
        }
        spdlog::info("ThirdPerson camera entered");
        return true;
    }

    bool ThirdPerson::OnExit()
    {
        m_Active = false;
        if (m_Events) {
            m_Events->Trigger(EventType::kOnExitThirdPerson);
        }
        spdlog::info("ThirdPerson camera exited");
        return true;
    }

    bool ThirdPerson::OnUpdate()
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
