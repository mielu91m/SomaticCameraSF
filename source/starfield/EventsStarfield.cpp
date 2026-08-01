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

#define _CRT_SECURE_NO_WARNINGS
#include "starfield/EventsStarfield.h"
#include "starfield/Hooks.h"
#include "starfield/Helper.h"
#include "starfield/SAFIntegration.h"
#include "starfield/PseudoFPState.h"
#include "SFSE/API.h"
#include "RE/P/PlayerCamera.h"
#include "RE/T/TESCamera.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiNode.h"
#include "RE/A/Actor.h"
#include "RE/A/ActorEquipManager.h"
#include "RE/B/BGSKeyword.h"
#include "RE/B/BGSKeywordForm.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESFile.h"
#include "RE/T/TESObjectARMO.h"
#include <fstream>
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <Windows.h>
#include <Xinput.h>
#pragma comment(lib, "Xinput9_1_0.lib")

#pragma push_macro("near")
#pragma push_macro("far")
#undef near
#undef far

namespace Patch {

    // Seat-exit grace state (see IsSeatExitPseudoGraceActive in
    // PseudoFPState.h). Managed by the EventsStarfield per-frame task;
    // read by PseudoFP.cpp to relax its ship-state bails for ~2s after
    // the player leaves the pilot seat so the base-game stand-up
    // animation stays in pseudo instead of vanilla TPP.
    static bool g_SeatExitGraceActive = false;
    static int g_SeatExitGraceFrames = 0;
    static bool g_WasInSeat = false;

    bool IsSeatExitPseudoGraceActive()
    {
        return g_SeatExitGraceActive && g_SeatExitGraceFrames > 0;
    }

    static void VLog(const char* fmt, va_list args)
    {
        char* userProfile = nullptr;
        size_t len = 0;
        _dupenv_s(&userProfile, &len, "USERPROFILE");
        std::string path = std::string(userProfile ? userProfile : "") + "\\Documents\\My Games\\Starfield\\SFSE\\SomaticCameraSF_debug.log";
        free(userProfile);
        std::ofstream log(path, std::ios::app);
        if (log) {
            time_t t = time(nullptr);
            struct tm tm;
            localtime_s(&tm, &t);
            char buf[64];
            strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
            log << "[" << buf << "] ";
            char msg[512];
            vsprintf_s(msg, fmt, args);
            log << msg << std::endl;
        }
    }

    static void LogFormatted(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        VLog(fmt, args);
        va_end(args);
    }

    static void Log(const char* msg) { LogFormatted("%s", msg); }

    // Left trigger analog aiming on gamepad (controller 0). GetAsyncKeyState
    // cannot see this - XInput has to be polled separately.
    static bool IsGamepadAiming(int threshold)
    {
        XINPUT_STATE state{};
        if (XInputGetState(0, &state) != ERROR_SUCCESS) {
            return false; // no controller connected
        }
        return state.Gamepad.bLeftTrigger > static_cast<BYTE>(threshold);
    }

    static std::string GetPluginDir()
    {
        char path[MAX_PATH];
        GetModuleFileNameA(GetModuleHandleA("SomaticCameraSF.dll"), path, MAX_PATH);
        std::string full(path);
        size_t pos = full.find_last_of("\\/");
        return (pos != std::string::npos) ? full.substr(0, pos + 1) : "";
    }

    static void StripValue(std::string& val)
    {
        // Strip inline comment (MO2's usvfs does NOT strip ; or // comments)
        size_t commentPos = val.find(';');
        if (commentPos != std::string::npos) {
            val = val.substr(0, commentPos);
        }
        commentPos = val.find("//");
        if (commentPos != std::string::npos) {
            val = val.substr(0, commentPos);
        }
        val.erase(0, val.find_first_not_of(" \t\r\n"));
        val.erase(val.find_last_not_of(" \t\r\n") + 1);
    }

    static float GetINIFloat(const char* section, const char* key, float defaultValue)
    {
        std::string iniPath = GetPluginDir() + "SomaticCameraSF.ini";
        char buf[32];
        GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), iniPath.c_str());
        if (strlen(buf) == 0)
            return defaultValue;
        std::string val(buf);
        StripValue(val);
        return static_cast<float>(atof(val.c_str()));
    }

    static int GetINIInt(const char* section, const char* key, int defaultValue)
    {
        std::string iniPath = GetPluginDir() + "SomaticCameraSF.ini";
        char buf[32];
        GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), iniPath.c_str());
        if (strlen(buf) == 0)
            return defaultValue;
        std::string val(buf);
        StripValue(val);
        return atoi(val.c_str());
    }

    static bool GetINIBool(const char* section, const char* key, bool defaultValue)
    {
        std::string iniPath = GetPluginDir() + "SomaticCameraSF.ini";
        char buf[32];
        GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), iniPath.c_str());
        LogFormatted("GetINIBool: reading [%s] %s from '%s' -> raw='%s'", section, key, iniPath.c_str(), buf);
        if (strlen(buf) == 0) {
            LogFormatted("GetINIBool: [%s] %s not found, returning default=%d", section, key, defaultValue ? 1 : 0);
            return defaultValue;
        }
        std::string val(buf);
        StripValue(val);
        bool result = (val == "1" || _stricmp(val.c_str(), "true") == 0 || _stricmp(val.c_str(), "yes") == 0);
        LogFormatted("GetINIBool: [%s] %s trimmed='%s' -> %d", section, key, val.c_str(), result ? 1 : 0);
        return result;
    }

    static void EnsureINI()
    {
        std::string iniPath = GetPluginDir() + "SomaticCameraSF.ini";
        if (GetFileAttributesA(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            WritePrivateProfileStringA("Settings", "fTPWorldFOV", "80.0", iniPath.c_str());
            WritePrivateProfileStringA("Settings", "fFPWorldFOV", "85.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fOffsetX", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fOffsetY", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fOffsetZ", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fFOV", "85.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "bUltraRigidHeadAttach", "false", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fHeadAttachGraceFrames", "45", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fUltraRigidMaxPlanarOffset", "0.60", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fUltraRigidHeightTolerance", "0.50", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fMaxYawDegrees", "90", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fNoseForward", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fForwardOffset", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fSideOffset", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fUpOffset", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "bAutoEquipHideHead", "1", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "bAutoEquipHideHeadSS", "1", iniPath.c_str());
            WritePrivateProfileStringA("Settings", "iToggleKey", "115", iniPath.c_str());
            WritePrivateProfileStringA("Settings", "iADSKey", "2", iniPath.c_str());
            WritePrivateProfileStringA("Settings", "iGamepadTriggerThreshold", "30", iniPath.c_str());
            Log("Created default SomaticCameraSF.ini");
        }
    }

    static RE::TESBoundObject* LookupHideHeadgearForm(const char* editorID)
    {
        auto* form = RE::TESForm::LookupByEditorID<RE::TESObjectARMO>(RE::BSFixedString(editorID));
        if (form) {
            LogFormatted("LookupHideHeadgear: found '%s' via EditorID", editorID);
            return form;
        }
        // EditorID not found. Log the plugin dump once for diagnostics,
        // but do NOT fall back to a FormID scan — that can match random
        // forms from other plugins and equip the wrong item.
        LogFormatted("LookupHideHeadgear: EditorID '%s' not found — plugin not loaded or forms not registered yet", editorID);
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (dh) {
            static bool dumpedFiles = false;
            if (!dumpedFiles) {
                dumpedFiles = true;
                LogFormatted("LookupHideHeadgear: dumping %u full-plugin files:", dh->compiledFileCollection.files.size());
                for (std::uint32_t i = 0; i < dh->compiledFileCollection.files.size(); i++) {
                    auto* file = dh->compiledFileCollection.files[i];
                    if (!file) continue;
                    LogFormatted("  full[%u] file '%s' compileIndex=%02X",
                        i, file->fileName, file->compileIndex);
                }
                LogFormatted("LookupHideHeadgear: dumping %u small (light/ESL) files:", dh->compiledFileCollection.smallFiles.size());
                for (std::uint32_t i = 0; i < dh->compiledFileCollection.smallFiles.size(); i++) {
                    auto* file = dh->compiledFileCollection.smallFiles[i];
                    if (!file) continue;
                    LogFormatted("  small[%u] file '%s'", i, file->fileName);
                }
                LogFormatted("LookupHideHeadgear: dumping %u medium files:", dh->compiledFileCollection.mediumFiles.size());
                for (std::uint32_t i = 0; i < dh->compiledFileCollection.mediumFiles.size(); i++) {
                    auto* file = dh->compiledFileCollection.mediumFiles[i];
                    if (!file) continue;
                    LogFormatted("  medium[%u] file '%s'", i, file->fileName);
                }
            }
        }
        // No FormID fallback — see comment above.
        return nullptr;
    }

    // Starfield stores the biped slot mask as a 64-bit BO64 bitfield
    // (64 slots; bit N = slot N, e.g. slot 36 = helmet, slot 35 = spacesuit,
    // slot 37 = pack, slot 3 = body). CommonLibSF's BIPED_MODEL only declares
    // a uint32_t at that address, so every bit >= 32 silently reads as 0.
    // Copy the full 8 bytes the engine actually stores.
    static std::uint64_t GetBipedSlots64(const RE::TESObjectARMO* armor)
    {
        if (!armor) return 0;
        std::uint64_t mask = 0;
        std::memcpy(&mask, &armor->bipedModelData.bipedObjectSlots, sizeof(mask));
        return mask;
    }

    // Checks if the player is wearing an integrated spacesuit that occupies
    // both body (bit 2 = 0x04) and head (bit 0 = 0x01) slots as a single item.
    // Equipping the invisible hide-headgear would conflict and unequip such a suit.
    static bool HasIntegratedSpacesuit(RE::PlayerCharacter* player)
    {
        if (!player) return false;
        bool found = false;
        player->ForEachEquippedItem([&](const RE::BGSInventoryItem& item) {
            auto* armor = item.object ? item.object->As<RE::TESObjectARMO>() : nullptr;
            if (armor) {
                const std::uint64_t slotMask = GetBipedSlots64(armor);
                if ((slotMask & 0x05) == 0x05) {
                    found = true;
                    return RE::BSContainer::ForEachResult::kStop;
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return found;
    }

    // Cached form pointers shared between EquipHideHeadgear and UnequipHideHeadgear.
    // Lookup is retried until both forms are found (plugin data may not be ready
    // on the very first call).
    static RE::TESBoundObject* g_HideHead = nullptr;
    static RE::TESBoundObject* g_HideHeadSS = nullptr;

    static void LookupHideHeadgearBoth()
    {
        if (!g_HideHead) {
            g_HideHead = LookupHideHeadgearForm("ICSF_Hide_Head");
            LogFormatted("LookupHideHeadgearBoth: hideHead=%p", (void*)g_HideHead);
        }
        if (!g_HideHeadSS) {
            g_HideHeadSS = LookupHideHeadgearForm("ICSF_Hide_Head_SS");
            LogFormatted("LookupHideHeadgearBoth: hideHeadSS=%p", (void*)g_HideHeadSS);
        }
    }

    static void EnsureHideHeadgearLookup()
    {
        LookupHideHeadgearBoth();
    }

    // Returns true if a specific hide-head item is currently equipped by the player.
    static bool IsHideItemEquipped(RE::PlayerCharacter* player, RE::TESBoundObject* item)
    {
        if (!player || !item) return false;
        bool equipped = false;
        player->ForEachEquippedItem([&](const RE::BGSInventoryItem& invItem) {
            if (invItem.object == item) {
                equipped = true;
                return RE::BSContainer::ForEachResult::kStop;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return equipped;
    }

    static bool GetAutoEquipHideHead()
    {
        return GetINIBool("PseudoFP", "bAutoEquipHideHead", true);
    }

    static bool GetAutoEquipHideHeadSS()
    {
        return GetINIBool("PseudoFP", "bAutoEquipHideHeadSS", true);
    }

// Saved original helmet pointer (CK slot 36 = biped mask bit 6 = 0x40).
    // One pointer is enough — spacesuit helmets are single-slot items.
    // Cached HelmetItem keyword to detect helmets regardless of slot mask.
    static RE::BGSKeyword* g_HelmetKeyword = nullptr;

    static RE::BGSKeyword* GetHelmetKeyword()
    {
        if (!g_HelmetKeyword) {
            g_HelmetKeyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(RE::BSFixedString("HelmetItem"));
            LogFormatted("GetHelmetKeyword: HelmetItem keyword = %p", (void*)g_HelmetKeyword);
        }
        return g_HelmetKeyword;
    }

    // Saves ALL equipped items (not just the first match), using
    // keyword check first, then slot mask fallback. The primary fallback
    // is the ACTUAL slot mask of the hide-head items themselves: whatever
    // slots the hide-head gear occupies, any equipped armor sharing one of
    // those bits IS the head gear we need to save+unequip. The hardcoded
    // 0x40/0x8000000 masks are only a legacy safety net (the "HelmetItem"
    // keyword does not exist in Starfield, so it resolves to null).
    // Unequips every match so the hide-head items can take their place.
    static std::vector<RE::TESBoundObject*> g_SavedOriginalHelmets;

    // Combined biped slot mask of the ICSF_Hide_Head / ICSF_Hide_Head_SS
    // items. Used as the authoritative "head slot" reference so detection
    // works regardless of which bits the player's actual helmet uses.
    static std::uint64_t GetHideHeadSlotMask()
    {
        std::uint64_t mask = 0;
        auto addItem = [&mask](RE::TESBoundObject* item) {
            if (!item) return;
            if (auto* armo = item->As<RE::TESObjectARMO>()) {
                mask |= GetBipedSlots64(armo);
            }
        };
        addItem(g_HideHead);
        addItem(g_HideHeadSS);
        return mask;
    }

    static void SaveAndUnequipOriginalHeadGear(RE::PlayerCharacter* player, RE::ActorEquipManager* mgr)
    {
        const bool hideHeadAlreadyEquipped = IsHideItemEquipped(player, g_HideHead) || IsHideItemEquipped(player, g_HideHeadSS);
        if (hideHeadAlreadyEquipped && !g_SavedOriginalHelmets.empty()) {
            Log("SaveAndUnequipOriginalHeadGear: hide-head item already equipped, keeping previously saved helmets");
            return;
        }

        g_SavedOriginalHelmets.clear();
        auto* helmetKeyword = GetHelmetKeyword();
        const std::uint64_t hideMask = GetHideHeadSlotMask();
        LogFormatted("SaveAndUnequipOriginalHeadGear: hideHeadSlotMask=%016llX keyword=%p", hideMask, (void*)helmetKeyword);
        if (!helmetKeyword) {
            Log("SaveAndUnequipOriginalHeadGear: HelmetItem keyword not found, falling back to slot mask");
        }
        // Starfield biped slots (bit N = slot N). Only the helmet (slot 36)
        // should be removed during pseudo activation — the spacesuit (35) and
        // pack (37) must stay equipped.
        constexpr std::uint64_t kHelmetSlot = 1ULL << 36; // slot 36 = helmet
        const std::uint64_t starfieldHeadMask = kHelmetSlot;

        // Collect matches first, THEN unequip. Calling UnequipObject while
        // iterating ForEachEquippedItem mutates the inventory mid-iteration,
        // which invalidates the iterator and hangs the game.
        std::vector<RE::TESBoundObject*> toUnequip;
        player->ForEachEquippedItem([&](const RE::BGSInventoryItem& item) {
            auto* armor = item.object ? item.object->As<RE::TESObjectARMO>() : nullptr;
            if (armor) {
                const std::uint64_t slots = GetBipedSlots64(armor);
                bool isHeadGear = false;
                if (helmetKeyword) {
                    isHeadGear = armor->HasKeyword(helmetKeyword);
                }
                if (!isHeadGear && hideMask) {
                    isHeadGear = (slots & hideMask) != 0;
                }
                if (!isHeadGear) {
                    isHeadGear = (slots & starfieldHeadMask) != 0; // Starfield helmet/spacesuit/pack
                }
                if (!isHeadGear) {
                    isHeadGear = (slots & 0x8000040) != 0; // legacy: bit 6 (0x40) or bit 27 (0x8000000)
                }
                if (isHeadGear) {
                    if (item.object == g_HideHead || item.object == g_HideHeadSS) {
                        LogFormatted("SaveAndUnequipOriginalHeadGear: skipping internal hide item %p", (void*)item.object);
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    toUnequip.push_back(item.object);
                    LogFormatted("SaveAndUnequipOriginalHeadGear: matched head gear %p (slotMask=%016llX)",
                        (void*)item.object, slots);
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });

        for (auto* obj : toUnequip) {
            g_SavedOriginalHelmets.push_back(obj);
            RE::BGSObjectInstance inst(obj, nullptr);
            mgr->UnequipObject(player, inst, nullptr, false, true, false, true, nullptr);
        }

        if (g_SavedOriginalHelmets.empty()) {
            Log("SaveAndUnequipOriginalHeadGear: no head gear found to save");
            Log("SaveAndUnequipOriginalHeadGear: dumping ALL equipped armor (slotMask / EditorID):");
            player->ForEachEquippedItem([&](const RE::BGSInventoryItem& item) {
                auto* armor = item.object ? item.object->As<RE::TESObjectARMO>() : nullptr;
                if (armor) {
                    const std::uint64_t slots = GetBipedSlots64(armor);
                    const char* edid = armor->formEditorID.c_str();
                    if (!edid || !edid[0]) edid = "?";
                    LogFormatted("  equipped armor %p formID=%08X slotMask=%016llX EditorID='%s' name='%s'",
                        (void*)item.object, armor->GetFormID(), slots, edid,
                        armor->GetFullName() ? armor->GetFullName() : "?");
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
    }

    static void RestoreOriginalHeadGear(RE::PlayerCharacter* player, RE::ActorEquipManager* mgr)
    {
        for (auto* saved : g_SavedOriginalHelmets) {
            if (saved) {
                RE::BGSObjectInstance inst(saved, nullptr);
                if (mgr->EquipObject(player, inst, nullptr, false, true, false, false, false)) {
                    LogFormatted("RestoreOriginalHeadGear: restored head gear %p", (void*)saved);
                } else {
                    LogFormatted("RestoreOriginalHeadGear: EquipObject returned false for %p", (void*)saved);
                }
            }
        }
        g_SavedOriginalHelmets.clear();
    }

    void EquipHideHeadgear()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            Log("EquipHideHeadgear: no player");
            return;
        }

        EnsureHideHeadgearLookup();

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) {
            Log("EquipHideHeadgear: no ActorEquipManager");
            return;
        }

        // Only strip the currently equipped helmet when auto-equip of the
        // spacesuit hide-head item is enabled in the INI. The user chose
        // bAutoEquipHideHeadSS as the master switch for helmet stripping:
        // when it is 0 the code must NOT unequip the helmet the player has
        // on (and there is nothing to save/restore), regardless of the
        // regular bAutoEquipHideHead option.
        if (GetAutoEquipHideHeadSS()) {
            SaveAndUnequipOriginalHeadGear(player, mgr);
        } else {
            g_SavedOriginalHelmets.clear();
            Log("EquipHideHeadgear: bAutoEquipHideHeadSS disabled, leaving equipped helmet in place");
        }

        bool anyAdded = false;
        bool anyEquipped = false;

        if (g_HideHead) {
            player->AddObjectToContainer(g_HideHead, RE::BSTSmartPointer<RE::ExtraDataList>(), 1, nullptr, RE::ITEM_TRANSFER_REASON::kAddWornItem);
            anyAdded = true;
            Log("EquipHideHeadgear: hideHead added to inventory");
            if (GetAutoEquipHideHead()) {
                if (!IsHideItemEquipped(player, g_HideHead)) {
                    RE::BGSObjectInstance inst(g_HideHead, nullptr);
                    if (mgr->EquipObject(player, inst, nullptr, false, true, false, false, false)) {
                        Log("EquipHideHeadgear: hideHead equipped via EquipObject");
                        anyEquipped = true;
                    } else {
                        Log("EquipHideHeadgear: EquipObject hideHead returned false");
                    }
                } else {
                    Log("EquipHideHeadgear: hideHead already equipped");
                }
            } else {
                Log("EquipHideHeadgear: hideHead auto-equip disabled by INI");
            }
        } else {
            Log("EquipHideHeadgear: hideHead is null");
        }

        if (g_HideHeadSS) {
            player->AddObjectToContainer(g_HideHeadSS, RE::BSTSmartPointer<RE::ExtraDataList>(), 1, nullptr, RE::ITEM_TRANSFER_REASON::kAddWornItem);
            anyAdded = true;
            Log("EquipHideHeadgear: hideHeadSS added to inventory");
            if (GetAutoEquipHideHeadSS()) {
                if (!IsHideItemEquipped(player, g_HideHeadSS)) {
                    RE::BGSObjectInstance inst(g_HideHeadSS, nullptr);
                    if (mgr->EquipObject(player, inst, nullptr, false, true, false, false, false)) {
                        Log("EquipHideHeadgear: hideHeadSS equipped via EquipObject");
                        anyEquipped = true;
                    } else {
                        Log("EquipHideHeadgear: EquipObject hideHeadSS returned false");
                    }
                } else {
                    Log("EquipHideHeadgear: hideHeadSS already equipped");
                }
            } else {
                Log("EquipHideHeadgear: hideHeadSS auto-equip disabled by INI");
            }
        } else {
            Log("EquipHideHeadgear: hideHeadSS is null");
        }

        if (anyAdded) {
            LogFormatted("EquipHideHeadgear: done (equipped=%d)", anyEquipped ? 1 : 0);
        }
    }

    void UnequipHideHeadgear()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            Log("UnequipHideHeadgear: no player");
            return;
        }

        if (HasIntegratedSpacesuit(player)) {
            Log("UnequipHideHeadgear: integrated spacesuit detected, skipping");
            return;
        }

        EnsureHideHeadgearLookup();

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) {
            Log("UnequipHideHeadgear: no ActorEquipManager");
            return;
        }

        bool anyUnequipped = false;

        if (g_HideHead) {
            if (IsHideItemEquipped(player, g_HideHead)) {
                RE::BGSObjectInstance inst(g_HideHead, nullptr);
                if (mgr->UnequipObject(player, inst, nullptr, false, true, false, true, nullptr)) {
                    Log("UnequipHideHeadgear: hideHead unequipped");
                    anyUnequipped = true;
                } else {
                    Log("UnequipHideHeadgear: UnequipObject hideHead returned false");
                }
            }
            std::uint32_t outHandle = 0;
            RE::RemoveItemRequest req;
            req.object = g_HideHead;
            req.count = 1;
            req.reason = RE::ITEM_TRANSFER_REASON::kScriptRemoveItem;
            player->RemoveItem(outHandle, req);
            Log("UnequipHideHeadgear: hideHead removed from inventory");
        } else {
            Log("UnequipHideHeadgear: hideHead is null");
        }

        if (g_HideHeadSS) {
            if (IsHideItemEquipped(player, g_HideHeadSS)) {
                RE::BGSObjectInstance inst(g_HideHeadSS, nullptr);
                if (mgr->UnequipObject(player, inst, nullptr, false, true, false, true, nullptr)) {
                    Log("UnequipHideHeadgear: hideHeadSS unequipped");
                    anyUnequipped = true;
                } else {
                    Log("UnequipHideHeadgear: UnequipObject hideHeadSS returned false");
                }
            }
            std::uint32_t outHandle = 0;
            RE::RemoveItemRequest req;
            req.object = g_HideHeadSS;
            req.count = 1;
            req.reason = RE::ITEM_TRANSFER_REASON::kScriptRemoveItem;
            player->RemoveItem(outHandle, req);
            Log("UnequipHideHeadgear: hideHeadSS removed from inventory");
        } else {
            Log("UnequipHideHeadgear: hideHeadSS is null");
        }

        if (!anyUnequipped) {
            Log("UnequipHideHeadgear: no hide items were equipped (removed from inventory anyway)");
        }

        RestoreOriginalHeadGear(player, mgr);
    }

    EventsStarfield::EventsStarfield() {}

    bool EventsStarfield::Setup()
    {
        if (m_Setup)
            return true;

        Log("Start");
        EnsureINI();

        float tppFOV = GetINIFloat("Settings", "fTPWorldFOV", 84.0f);
        float fpFOV = GetINIFloat("Settings", "fFPWorldFOV", 85.0f);
        float pseudoFOV = GetINIFloat("PseudoFP", "fFOV", 85.0f);

        auto task = SFSE::GetTaskInterface();
        if (task) {
            task->AddPermanentTask([tppFOV, fpFOV, pseudoFOV]() {
                auto camera = RE::PlayerCamera::GetSingleton();
                if (!camera) return;

                // === SAF integration ===
                // If SAF is playing an animation on the player while in a
                // non-TPP state (kFurniture), we keep pseudo active so the
                // camera stays at the player's head. This requires SAF to
                // export SAFAPI_IsPlayingAnimation.
                auto* player = RE::PlayerCharacter::GetSingleton();
                bool safPlayingRaw = false;
                static int safLogThrottle = 0;
                if (player) {
                    safPlayingRaw = SAFIntegration::IsSAFAnimationPlaying(player);
                }

                // Debounce: SAFAPI_IsPlayingAnimation can momentarily report
                // false for a frame or two during SAF's own internal pose
                // transitions (e.g. loop restarts, pose-to-pose blends).
                // A raw level-triggered read of that flag causes the SAF
                // guard in DetourSetCameraState to open for exactly those
                // frames, letting the engine slip into a Free* camera
                // state. That slip does NOT self-correct once it happens -
                // ForceCameraToHead() fighting the position every frame
                // afterward is what produces the "ghost you can nudge
                // before it snaps back" effect reported around animation
                // loop restarts. Require a short streak of consecutive
                // false reads before we actually drop the SAF flag.
                static int safFalseStreak = 0;
                constexpr int kSAFDropGraceFrames = 8;
                if (safPlayingRaw) {
                    safFalseStreak = 0;
                } else {
                    safFalseStreak++;
                }
                bool safPlaying = safPlayingRaw ||
                    (Patch::g_SAFAnimationPlaying && safFalseStreak <= kSAFDropGraceFrames);

                Patch::g_SAFAnimationPlaying = safPlaying;
                safLogThrottle++;
                if (safPlaying && (safLogThrottle % 60) == 0) {
                    LogFormatted("PseudoFP: SAF animation detected on player");
                }
                if (!safPlayingRaw && safFalseStreak > 0 && safFalseStreak <= kSAFDropGraceFrames) {
                    LogFormatted("PseudoFP: SAF raw=false, riding out grace frame %d/%d", safFalseStreak, kSAFDropGraceFrames);
                }

                // g_PseudoUserEnabled: user wants pseudo (set by F4)
                // g_PseudoFPPActive:  pseudo is actually running this frame
                static bool g_PseudoUserEnabled = false;

                // --- F4 toggle ---
    // Read toggle key from INI (default VK_F4 = 115).
    // Supports VK codes (e.g., 115=F4, 112=F1, 36=Home, 35=End, 33=PageUp, 34=PageDown)
    // or ASCII letter codes (65=A, 66=B, etc.).
    static int g_ToggleKey = 0;
    static int g_ToggleKeyCheckFrames = 0;
    g_ToggleKeyCheckFrames++;
    if (g_ToggleKey == 0 || (g_ToggleKeyCheckFrames % 120) == 0) {
        g_ToggleKey = static_cast<int>(GetINIInt("Settings", "iToggleKey", 115));
        if (g_ToggleKey < 1 || g_ToggleKey > 255) {
            g_ToggleKey = 115; // VK_F4
        }
    }

    static bool wasToggleDown = false;
    bool isToggleDown = (GetAsyncKeyState(g_ToggleKey) & 0x8000) != 0;
    if (isToggleDown && !wasToggleDown) {
        g_PseudoUserEnabled = !g_PseudoUserEnabled;
        LogFormatted("PseudoFP: user %s by key VK=%d", g_PseudoUserEnabled ? "ENABLED" : "DISABLED", g_ToggleKey);
        if (g_PseudoUserEnabled) {
            if (!IsInVehicleCameraState(camera)) {
                camera->ForceThirdPerson();
            }
            EquipHideHeadgear();
        } else {
            // Always restore the helmet when the user toggles pseudo
            // off, even if g_PseudoFPPActive was already cleared by
            // an automatic deactivation (ship, skeleton change, etc.).
            UnequipHideHeadgear();
            if (g_PseudoFPPActive) {
                g_PseudoFPPActive = false;
                ResetPseudoFPPState();
            }
        }
    }
    wasToggleDown = isToggleDown;

    // --- ADS key (default VK_RBUTTON = 2) ---
    // We poll the physical button instead of relying solely on the engine's
    // kIronSights camera state, because aiming while the game is in
    // third-person (which is what our pseudo camera actually is under the
    // hood) generally does NOT change CameraState to kIronSights - the
    // engine just zooms while staying in kThirdPerson. That meant the old
    // isIronSightsNow-only check never fired when aiming from pseudo.
    static int g_ADSKey = 0;
    static int g_ADSKeyCheckFrames = 0;
    g_ADSKeyCheckFrames++;
    if (g_ADSKey == 0 || (g_ADSKeyCheckFrames % 120) == 0) {
        g_ADSKey = static_cast<int>(GetINIInt("Settings", "iADSKey", 2));
        if (g_ADSKey < 1 || g_ADSKey > 255) {
            g_ADSKey = 2; // VK_RBUTTON
        }
    }
    const bool isADSKeyDown = (GetAsyncKeyState(g_ADSKey) & 0x8000) != 0;

    // --- Gamepad left trigger (default threshold 30/255) ---
    static int g_GamepadThreshold = -1;
    static int g_GamepadThresholdCheckFrames = 0;
    g_GamepadThresholdCheckFrames++;
    if (g_GamepadThreshold < 0 || (g_GamepadThresholdCheckFrames % 120) == 0) {
        g_GamepadThreshold = GetINIInt("Settings", "iGamepadTriggerThreshold", 30);
        if (g_GamepadThreshold < 0 || g_GamepadThreshold > 255) {
            g_GamepadThreshold = 30;
        }
    }
    const bool isGamepadAimingNow = IsGamepadAiming(g_GamepadThreshold);

                // --- Determine state ---
                const bool isTPP = camera->IsInThirdPerson();
                const bool isIronSightsNow = camera->QCameraEquals(RE::CameraState::kIronSights);
// Ship camera states (kFlight, kShipAction, etc.) — also treat
			 // being in the pilot seat as "in ship context" so FPP in
			 // the cockpit is handled the same way as FPP during kFlight.
			 // isShip is used below to decide whether pseudo should be
			 // active and whether FPP should be allowed to stay (ForceThirdPerson
			 // must NOT fire for pilot-seat cockpit FPP).
                 const bool isShip = IsInShipCameraState(camera) || (player && IsPlayerInShipPilotSeat(player));
                // Disable pseudo during ship flight ONLY when the player has
                // left the pilot seat (standing in a moving ship). While seated
                // in the pilot seat, PseudoFP.cpp keeps the rig active.
                // During an SAF stand-up/sit-down animation the player has
                // ALREADY left the seat but the camera may still be in a ship
                // state for the whole animation — treat that as still pseudo-
                // eligible so the stand-up stays in pseudo, not vanilla TPP.
                //
                // Seat-exit grace: the base-game pilot-seat stand-up animation
                // is NOT an SAF animation (safPlaying is false for it), and the
                // player leaves the furniture the instant it starts. Without a
                // grace window that fires on the true→false seat transition,
                // shouldDisablePseudo goes true on the very first stand-up frame
                // and the whole animation plays in vanilla TPP. Keep pseudo
                // eligible for ~2s after the player leaves the seat so the
                // base-game stand-up/sit-down stays in pseudo like the SAF one.
                constexpr int kSeatExitGraceMaxFrames = 120;
                const bool inSeatNow = player && IsPlayerInShipPilotSeat(player);
                if (inSeatNow) {
                    g_WasInSeat = true;
                    g_SeatExitGraceActive = false;
                    g_SeatExitGraceFrames = 0;
                } else if (g_WasInSeat) {
                    // Just left the pilot seat — start the stand-up grace.
                    g_WasInSeat = false;
                    g_SeatExitGraceFrames = kSeatExitGraceMaxFrames;
                    g_SeatExitGraceActive = true;
                } else if (g_SeatExitGraceActive) {
                    if (g_SeatExitGraceFrames > 0) {
                        g_SeatExitGraceFrames--;
                    } else {
                        g_SeatExitGraceActive = false;
                    }
                }
                const bool seatExitGraceNow = g_SeatExitGraceActive && g_SeatExitGraceFrames > 0;
                const bool shouldDisablePseudo = isShip && !safPlaying && !inSeatNow && !seatExitGraceNow;
                const bool isVehicle = IsInVehicleCameraState(camera);
                const bool isFPP = camera->IsInFirstPerson();
                const bool isFurniture = camera->QCameraEquals(RE::CameraState::kFurniture);
                // Aiming = real kIronSights state OR the ADS button/trigger
                // is physically held down (covers TPP/pseudo aiming, which
                // doesn't flip CameraState on its own, for both mouse and pad).
                const bool isAimingRaw = isIronSightsNow || ((isADSKeyDown || isGamepadAimingNow) && (isTPP || isFPP));
                // Hard-block aiming detection while SAF plays. If the engine
                // reports kIronSights (or the ADS button/trigger happens to
                // read as held) during a scripted SAF pose, isAimingNow used
                // to still go true and the ADS handler below would call
                // camera->ForceFirstPerson() - i.e. OUR OWN CODE actively
                // switching from the pseudo camera to the engine's real FPP
                // camera, which looks exactly like "the camera escapes the
                // moment SAF starts" from the outside. SAF sessions must
                // never be interpreted as aiming, full stop.
                if (isAimingRaw && safPlaying) {
                    LogFormatted("PseudoFP: aiming input detected DURING SAF (ironSights=%d adsKey=%d gamepad=%d) - suppressed, would have called ForceFirstPerson",
                        isIronSightsNow ? 1 : 0, isADSKeyDown ? 1 : 0, isGamepadAimingNow ? 1 : 0);
                }
                const bool isAimingNow = isAimingRaw && !safPlaying;
                const bool isBodyState = isTPP || isFPP || isFurniture || isAimingNow || isShip || isVehicle;

                // Track aiming transitions for ADS-in-FPP
                static bool g_WasAiming = false;

                bool shouldPseudo = false;
                if (g_PseudoUserEnabled) {
                    // Pseudo is ONLY toggled by the hotkey (F4). Camera state
                    // transitions (Free*, furniture, etc.) must NOT disable
                    // it — otherwise SAF animation startup briefly changes
                    // state before reporting the animation, killing pseudo.
                    // The only exceptions are aiming (ADS) and genuine ship
                    // flight (when NOT seated in the pilot seat). When seated,
                    // PseudoFP.cpp keeps the rig active for cockpit FPP.
                    // When the user releases the trigger / exits the
                    // ship, pseudo resumes automatically.
                    shouldPseudo = !isAimingNow && !shouldDisablePseudo;
                    // Force FPP back to TPP - including during SAF. This
                    // used to skip while safPlaying was true (to avoid
                    // fighting a brief FPP flash right at SAF startup), but
                    // that let the engine settle into and STAY in real FPP
                    // for the entire SAF session once it flipped (confirmed
                    // by CAM_STATE logs: idx=0 TPP=0 FP=1 saf=1 persisting
                    // for the whole animation). While in real engine FPP,
                    // the player's normal camera-cycle input (TPP
                    // near/far/FPP) is processed by the real camera system
                    // instead of being masked by the pseudo rig - exactly
                    // "the pseudo camera leaves the head and lets you
                    // switch between the game's own cameras". Pseudo must
                    // mean pseudo, full stop, any time it's not a genuine
                    // aim or ship flight.
                    if (isFPP && !g_WasAiming && !isAimingNow && !isShip) {
                        camera->ForceThirdPerson();
                    }
                    if (safPlaying && Patch::IsInFreeCameraState(camera)) {
                        camera->ForceThirdPerson();
                    }
                }

                // ADS: when pseudo is active and player aims (PPM held), switch to real FPP
                // (isAimingNow is already forced false while safPlaying, so this
                // branch cannot fire during a SAF animation.)
                if (g_PseudoUserEnabled && isAimingNow) {
                    if (!isFPP) {
                        camera->ForceFirstPerson();
                        LogFormatted("PseudoFP: ADS -> FPP");
                    }
                } else if (g_PseudoUserEnabled && g_WasAiming && !isAimingNow) {
                    if (isFPP) {
                        camera->ForceThirdPerson();
                        LogFormatted("PseudoFP: ADS end -> TPP (restore pseudo)");
                    }
                }
                g_WasAiming = isAimingNow;

                // --- Active recovery from a Free* camera slip ---
                // Belt-and-braces: if SAF is (debounced) playing but the
                // engine is already sitting in a Free* camera state -
                // whether from a slip that predates the debounce above,
                // or a state change that didn't go through
                // DetourSetCameraState at all - force it straight back to
                // third person instead of only correcting world position
                // every frame. This is what actually returns input control
                // to the pseudo rig rather than leaving a movable "ghost".
                if (g_PseudoUserEnabled && safPlaying && Patch::IsInFreeCameraState(camera)) {
                    camera->ForceThirdPerson();
                    LogFormatted("PseudoFP: recovered from Free camera state during SAF (idx check)");
                }

                // --- Apply or disable pseudo ---
                if (shouldPseudo) {
                    if (!g_PseudoFPPActive) {
                        g_PseudoFPPActive = true;
                        LogFormatted("PseudoFP: activated (TPP or SAF)");
                    }
                    // Apply rig for both TPP and SAF non-TPP states
                    auto* tesCam = static_cast<RE::TESCamera*>(camera);
                    ApplyPseudoFPPRig(tesCam, nullptr);
                } else {
                    // Disable delay: when shouldPseudo becomes false (e.g.
                    // a brief camera-state transition during SAF startup
                    // before IsSAFAnimationPlaying reports true) but the
                    // user did NOT explicitly aim, wait up to 15 frames
                    // before actually disabling pseudo.  During ADS
                    // (isAimingNow) the user intentionally wants real FPP,
                    // so skip the delay and disable immediately.
                    static int g_DisableDelayFrames = 0;
                    constexpr int kDisableDelayMax = 15;
                    if (g_PseudoUserEnabled && g_PseudoFPPActive && !isAimingNow && !shouldDisablePseudo && g_DisableDelayFrames < kDisableDelayMax) {
                        g_DisableDelayFrames++;
                        LogFormatted("PseudoFP: disable deferred %d/%d", g_DisableDelayFrames, kDisableDelayMax);
                    } else {
                        g_DisableDelayFrames = 0;
                        if (g_PseudoFPPActive) {
                            // During ADS the camera switches to real FPP for
                            // aiming. Keep the hide-head gear equipped the whole
                            // time so the helmet stays hidden when pseudo resumes.
                            // Only a genuine disable (ship flight / non-TPP without
                            // SAF / user toggle) may strip the hide-head gear.
                            if (!isAimingNow) {
                                UnequipHideHeadgear();
                            }
                            g_PseudoFPPActive = false;
                            ClearPseudoCameraPointers();
                            LogFormatted(isShip ? "PseudoFP: disabled (entered ship)" : "PseudoFP: disabled (non-TPP without SAF)");
                        }
                        if (!isBodyState) {
                            // Skip TPP-specific logic below
                            return;
                        }
                    }
                }

                // --- Log camera state ---
                static int stateLogCounter = 0;
                stateLogCounter++;
                if ((stateLogCounter % 60) == 0) {
                    auto* tesCam = static_cast<RE::TESCamera*>(camera);
                    uint32_t stateIdx = 0xFF;
                    for (uint32_t i = 0; i < RE::CameraState::kTotal; i++) {
                        if (tesCam->currentState == camera->cameraStates[i]) {
                            stateIdx = i;
                            break;
                        }
                    }
                    LogFormatted("CAM_STATE: idx=%u TPP=%d FP=%d pseudo=%d user=%d saf=%d",
                        stateIdx, isTPP ? 1 : 0, isFPP ? 1 : 0,
                        g_PseudoFPPActive ? 1 : 0, g_PseudoUserEnabled ? 1 : 0,
                        safPlaying ? 1 : 0);
                }

                // --- FOV switching ---
                static int lastState = -1;
                int state = isTPP ? 1 : 0;
                if (state != lastState && !g_PseudoFPPActive) {
                    lastState = state;
                    auto prefs = RE::INIPrefSettingCollection::GetSingleton();
                    if (!prefs) return;
                    if (isTPP) {
                        prefs->SetSetting("fTPWorldFOV:Camera", tppFOV);
                        LogFormatted("TPP FOV set to %.1f", tppFOV);
                    } else {
                        prefs->SetSetting("fFPWorldFOV:Camera", fpFOV);
                        LogFormatted("FP FOV set to %.1f", fpFOV);
                    }
                }

                // Pseudo-FPP FOV
                static float lastPseudoFOV = -1.0f;
                if (g_PseudoFPPActive && std::fabs(pseudoFOV - lastPseudoFOV) > 0.01f) {
                    lastPseudoFOV = pseudoFOV;
                    auto prefs = RE::INIPrefSettingCollection::GetSingleton();
                    if (prefs) {
                        prefs->SetSetting("fTPWorldFOV:Camera", pseudoFOV);
                        LogFormatted("PseudoFP FOV set to %.1f", pseudoFOV);
                    }
                } else if (!g_PseudoFPPActive) {
                    lastPseudoFOV = -1.0f;
                }

                // Aggressive camera pin: force head position at the VERY END
                // of the frame, after all mods including SAF have run.
                if (g_PseudoFPPActive) {
                    auto* tesCam = static_cast<RE::TESCamera*>(camera);
                    ApplyPseudoFPPRig(tesCam, nullptr);
                }

            });
            Log("Task registered");
        }

        m_Setup = true;
        Log("Setup done");
        return true;
    }

    bool EventsStarfield::Remove()
    {
        m_Setup = false;
        return true;
    }

}

#pragma pop_macro("near")
#pragma pop_macro("far")
