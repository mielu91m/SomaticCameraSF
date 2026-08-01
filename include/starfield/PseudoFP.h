/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "RE/N/NiAVObject.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiPoint.h"
#include "RE/T/TESCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/P/PlayerCamera.h"

namespace Patch {

    // Hook function pointers
    extern void (*origUpdate)(RE::TESCamera*);
    extern void (*origTPSUpdate)(void*);
    extern void* g_origFPSUpdate;
    extern void* g_origSetCameraState;

    // NiNode hook pointers
    extern void* g_origNiNodeUWD;
    extern void* g_origNiNodeUTB;
    extern void* g_origNiNodeUT;
    extern bool g_NiNodeHooksInstalled;
    extern void* g_ninodeUWDAddr;
    extern void* g_ninodeUTBAddr;
    extern void* g_ninodeUTAddr;
    void EnableNiNodeHooks();
    void DisableNiNodeHooks();

    // NiAVObject vtable hook pointers
    extern void* g_origUpdateWorldData;
    extern void* g_origUpdateTransformAndBounds;
    extern void* g_origUpdateTransforms;

    // Saved camera transforms for restoration when pseudo is deactivated.
    // The engine's kFurniture Update may not reposition the camera every
    // frame (only initializes once at state entry); pseudo overwrites the
    // transforms each frame, and without restoring them the camera stays
    // at whatever stale position pseudo last wrote after deactivation.
    extern RE::NiPoint3 g_SavedRootLocal;
    extern RE::NiPoint3 g_SavedNiCamLocal;
    extern bool g_SavedTransformsValid;

    // Force camera cache
     extern int g_LastForceFrame;
     extern RE::NiPoint3 g_CachedForcePosition;
     extern RE::NiMatrix3 g_CachedForceRotation;
     extern bool g_CachedForceRotationValid;
     extern int g_ColdStartSettleFrames;
     extern int g_PostForceSettle;
    extern bool g_CachedForceRotationValid;
    extern RE::NiPoint3 g_PrevSmoothDiff;

    // Functions
    bool ApplyPseudoFPPRig(RE::TESCamera* tesCam, void* tpsThis = nullptr);
    void ForceCameraToHead();

    // Applies (or refreshes) the SAF pitch-sync rotation onto cr/niCam.
    // Shared by ApplyPseudoFPPRig (fresh computation) and ForceCameraToHead
    // (re-assertion within the same logical frame, using the cached value)
    // so the synced pitch survives whatever later engine update passes
    // would otherwise overwrite it with plain mouse-look rotation - exactly
    // the same reason ForceCameraToHead already has to keep re-asserting
    // the translate.
    void ApplySAFRotationSync(RE::NiAVObject* cr, RE::NiAVObject* niCam, bool recompute);
    RE::NiMatrix3 ComputeSAFRigidEyeWorldRotation();
    float ComputeSAFHeadPitchRadians();
    RE::NiMatrix3 BuildYawPreservingPitchRotation(const RE::NiMatrix3& currentRot, float targetPitchRad);
    void RestorePseudoRig(RE::NiAVObject* a_this);
    void SetNiNodeHooksActive(bool active);
    void EnsureVanillaSafetyNet();

    // Detour functions
    void DetourUpdate(RE::TESCamera* a_this);
    void DetourTPSUpdate(void* a_this);
    void DetourFPSUpdate(void* a_this, float a_deltaTime);
    void DetourSetCameraState(RE::PlayerCamera* a_this, RE::CameraState a_newState);
    void* DetourUpdateWorldData(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
    void* DetourUpdateTransformAndBounds(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
    void* DetourUpdateTransforms(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
    void* DetourNiNodeUWD(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
    void* DetourNiNodeUTB(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
    void* DetourNiNodeUT(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
}
