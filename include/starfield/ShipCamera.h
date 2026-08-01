/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <cstdint>
#include "RE/N/NiCamera.h"
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESCamera.h"

namespace Patch
{
    bool IsInShipCameraState(RE::PlayerCamera* camera);
    bool IsPlayerInShipPilotSeat(RE::PlayerCharacter* player);
    bool IsPlayerOccupyingShipPilotSeat(RE::PlayerCharacter* player);
    void RegisterKnownShipPilotSeat(std::uint32_t a_baseFormID);

}
