/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <functional>
#include <vector>
#include <string>
#include <unordered_map>

namespace Camera {

    enum class EventType : uint32_t {
        kOnEnterFirstPerson = 0,
        kOnExitFirstPerson,
        kOnEnterThirdPerson,
        kOnExitThirdPerson,
        kOnEnterTransition,
        kOnExitTransition,
        kOnEnterHorse,
        kOnExitHorse,
        kOnEnterShip,
        kOnExitShip,
        kOnEnterFurniture,
        kOnExitFurniture,
        kOnEnterRagdoll,
        kOnExitRagdoll,
        kOnEnterDeathCinematic,
        kOnExitDeathCinematic,
        kOnCameraUpdate,
        kOnForceFirstPerson,
        kOnForceThirdPerson,
        kTotal
    };

    class Events {

    public:
        using EventCallback = std::function<void()>;

        Events();
        ~Events() = default;

        void Register(EventType type, EventCallback callback);
        void Unregister(EventType type, EventCallback callback);
        void Trigger(EventType type) const;

    private:
        std::unordered_map<EventType, std::vector<EventCallback>> m_Callbacks;
    };
}
