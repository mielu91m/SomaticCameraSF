/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <cstdint>

namespace RE {
    class NiAVObject;
}

namespace Patch {

    RE::NiAVObject* GetCachedHeadBone();

    /// Must be called whenever pseudo camera deactivates (g_PseudoFPPActive = false)
    /// so the original helmet is restored to the player.
    void UnequipHideHeadgear();
    void EquipHideHeadgear();

    class EventsStarfield {

    public:
        EventsStarfield();
        ~EventsStarfield() = default;

        EventsStarfield(const EventsStarfield&) = delete;
        EventsStarfield& operator=(const EventsStarfield&) = delete;
        EventsStarfield(EventsStarfield&&) = delete;
        EventsStarfield& operator=(EventsStarfield&&) = delete;

        bool Setup();
        bool Remove();

    private:
        bool m_Setup = false;
    };

}
