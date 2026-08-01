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

#include "starfield/ShipCamera.h"
#include "starfield/PseudoFPState.h"
#include "RE/B/BGSKeywordForm.h"
#include "RE/T/TESForm.h"
#include <vector>

namespace Patch
{
    static std::vector<std::uint32_t> g_LearnedShipPilotSeatFormIDs = { 0x00184717, 0x00005A98 };

    void RegisterKnownShipPilotSeat(std::uint32_t a_baseFormID)
    {
        for (auto id : g_LearnedShipPilotSeatFormIDs) {
            if (id == a_baseFormID) return;
        }
        g_LearnedShipPilotSeatFormIDs.push_back(a_baseFormID);
        LogFormatted("SHIP_SEAT_LEARNED: baseObjFormID=%08X (now recognized as pilot seat)", a_baseFormID);
    }

    bool IsPlayerOccupyingShipPilotSeat(RE::PlayerCharacter* player)
    {
        if (!player) return false;
        auto* proc = player->currentProcess;
        if (!proc || !proc->middleHigh || !proc->middleHigh->occupiedFurniture) return false;
        auto furnitureRef = proc->middleHigh->occupiedFurniture.get();
        if (!furnitureRef) return false;
        auto* baseObj = furnitureRef->GetBaseObject().get();
        if (!baseObj) return false;
        for (auto id : g_LearnedShipPilotSeatFormIDs) {
            if (baseObj->GetFormID() == id) return true;
        }
        // Also check FurnitureCantWait keyword (pilot seats across all ship types).
        static RE::BGSKeyword* furnitureCantWaitKeyword = nullptr;
        if (!furnitureCantWaitKeyword) {
            furnitureCantWaitKeyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(RE::BSFixedString("FurnitureCantWait"));
        }
        if (furnitureCantWaitKeyword && furnitureRef->HasKeyword(furnitureCantWaitKeyword)) {
            return true;
        }
        return false;
    }

    bool IsPlayerInShipPilotSeat(RE::PlayerCharacter* player)
    {
        if (!player) return false;
        auto* proc = player->currentProcess;
        if (!proc || !proc->middleHigh) return false;

        auto& furnitureHandle = proc->middleHigh->occupiedFurniture
            ? proc->middleHigh->occupiedFurniture
            : proc->middleHigh->currentFurniture;
        if (!furnitureHandle) return false;
        auto furnitureRef = furnitureHandle.get();
        if (!furnitureRef) return false;

        auto* baseObj = furnitureRef->GetBaseObject().get();
        if (!baseObj) return false;

        const std::uint32_t baseFormID = baseObj->GetFormID();

        // DIAGNOSTIC: log every furniture check
        static int seatLogThrottle = 0;
        seatLogThrottle++;
        if ((seatLogThrottle % 60) == 0) {
            LogFormatted("SEAT_CHECK: furnitureFormID=%08X baseObjFormID=%08X occupied=%d current=%d",
                furnitureRef->GetFormID(), baseFormID,
                (proc->middleHigh->occupiedFurniture ? 1 : 0),
                (proc->middleHigh->currentFurniture ? 1 : 0));
        }

        // DIAGNOSTIC: throttle log for performance (every 60 calls)
        static int seatMatchThrottle = 0;
        seatMatchThrottle++;
        for (auto id : g_LearnedShipPilotSeatFormIDs) {
            if (baseFormID == id) {
                if ((seatMatchThrottle % 60) == 0) {
                    LogFormatted("SEAT_MATCH: found in learned list (FormID=%08X)", baseFormID);
                }
                return true;
            }
        }

        static RE::BGSKeyword* shipPilotSeatKeyword = nullptr;
        if (!shipPilotSeatKeyword) {
            shipPilotSeatKeyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(RE::BSFixedString("ShipPilotSeat"));
            LogFormatted("SEAT_KEYWORD: shipPilotSeatKeyword=%p", (void*)shipPilotSeatKeyword);
        }
        if (shipPilotSeatKeyword && furnitureRef->HasKeyword(shipPilotSeatKeyword)) {
            LogFormatted("SEAT_KEYWORD_MATCH: furniture has ShipPilotSeat keyword (FormID=%08X)", baseFormID);
            RegisterKnownShipPilotSeat(baseFormID);
            return true;
        }

        // Additional check: FurnitureCantWait keyword (used by pilot seats across
        // all ship types, including DLC and custom ships that don't use ShipPilotSeat).
        static RE::BGSKeyword* furnitureCantWaitKeyword = nullptr;
        if (!furnitureCantWaitKeyword) {
            furnitureCantWaitKeyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(RE::BSFixedString("FurnitureCantWait"));
            LogFormatted("SEAT_KEYWORD: furnitureCantWaitKeyword=%p", (void*)furnitureCantWaitKeyword);
        }
        if (furnitureCantWaitKeyword && furnitureRef->HasKeyword(furnitureCantWaitKeyword)) {
            LogFormatted("SEAT_KEYWORD_MATCH: furniture has FurnitureCantWait keyword (FormID=%08X)", baseFormID);
            RegisterKnownShipPilotSeat(baseFormID);
            return true;
        }

        static int logThrottle = 0;
        logThrottle++;
        if ((logThrottle % 60) == 0) {
            LogFormatted("SHIP_SEAT_CHECK_MISS: furnitureFormID=%08X baseObjFormID=%08X (not yet a known pilot seat)",
                furnitureRef->GetFormID(), baseFormID);
        }

        return false;
    }

    bool IsInShipCameraState(RE::PlayerCamera* camera)
    {
        if (!camera) return false;
        return camera->QCameraEquals(RE::CameraState::kFlight) ||
               camera->QCameraEquals(RE::CameraState::kShipAction) ||
               camera->QCameraEquals(RE::CameraState::kShipTargeting) ||
               camera->QCameraEquals(RE::CameraState::kShipCombatOrbit) ||
               camera->QCameraEquals(RE::CameraState::kShipFarTravel);
    }

    bool g_FlightLatched = false;
    bool g_WasOccupied = false;
}
