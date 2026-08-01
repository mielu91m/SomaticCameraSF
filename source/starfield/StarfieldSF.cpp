/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "starfield/StarfieldSF.h"
#include "systems/Config.h"
#include "plugin.h"
#include "starfield/Hooks.h"
#include <fstream>
#include <ctime>
#include <cstdarg>
#include <cstdlib>

namespace Patch {

    StarfieldSF::StarfieldSF() {}

    bool StarfieldSF::Load(Systems::Config* config)
    {
        if (m_Loaded)
            return true;

        LogFormatted("StarfieldSF::Load begin config=%p", static_cast<void*>(config));

        // Step 1: Addresses
        m_Addresses = std::make_unique<Addresses>();
        if (!m_Addresses->Load()) {
            LogFormatted("Addresses::Load failed");
            return false;
        }
        LogFormatted("Addresses::Load ok");

        // Step 2: Hooks (sets up TESCamera::Update hook from vtable)
        m_Hooks = std::make_unique<Hooks>();
        if (!m_Hooks->Setup()) {
            LogFormatted("Hooks::Setup failed");
            return false;
        }
        LogFormatted("Hooks::Setup ok");

        // Step 3: SomaticCameraSF
        m_ImprovedCamera = std::make_unique<ImprovedCameraSF>();
        if (!m_ImprovedCamera->Load(config->General())) {
            LogFormatted("SomaticCameraSF::Load failed");
            return false;
        }
        LogFormatted("SomaticCameraSF::Load ok");

        // Step 5: EventsStarfield (permanent task - overwrites currentState + zoom)
        m_Events = std::make_unique<EventsStarfield>();
        if (!m_Events->Setup()) {
            LogFormatted("EventsStarfield::Setup failed");
            return false;
        }
        LogFormatted("EventsStarfield::Setup ok");

        m_Loaded = true;
        LogFormatted("StarfieldSF::Load success");
        return true;
    }
}
