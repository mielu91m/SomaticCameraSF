/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <cstring>
#include "RE/N/NiAVObject.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiPoint.h"
#include "RE/T/TESCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/P/PlayerCamera.h"
#include "RE/B/BSInputEnableLayer.h"
#include "starfield/ShipCamera.h"

namespace Patch {

    // Head bone globals
    extern RE::NiAVObject* g_HeadBone;
    extern RE::NiAVObject* g_HeadAnchorNode;
    extern RE::NiAVObject* g_HeadMesh;
    extern RE::NiAVObject* g_PrevSkeletonRoot;
    extern RE::NiAVObject* g_HideHeadNode;
    extern bool g_HideHeadNodeValid;
    extern RE::NiPoint3 g_HeadAnchorLocalOffset;
    extern RE::NiPoint3 g_LastValidHeadAnchorWorld;
    extern bool g_HasLastValidHeadAnchorWorld;
    extern bool g_HasSAFRigidSmoothedHeadPos;
    extern bool g_HasSAFLastRealFrameHeadPos;
    extern bool g_HasSAFSmoothedHeadPos;
    extern int g_SAFSceneAgeTicks;
    extern int g_SAFPendingTreeWalkCountdown;
    extern RE::NiPoint3 g_LastRawUltraRigidAnchor;
    extern bool g_HasLastRawUltraRigidAnchor;
    extern RE::NiPoint3 g_SAFPinnedCameraLocalOffset;
    extern bool g_HasSAFPinnedCameraLocalOffset;
    extern float g_EyeHeight;
    extern bool g_HasEyeHeight;
    extern bool g_WasHeadAppCulled;
    extern bool g_HasSavedHeadCull;
    extern uint32_t g_HeadAnchorMissCount;
    extern bool g_ColdStartPseudoResetPending;

    // SAF input lock: while a SAF animation plays we disable player movement
    // input (walk/jump/sneak/fight) so the actor is rooted and stays glued to
    // the animated body instead of being driven around by WASD (the "ghost").
    extern RE::BSInputEnableLayer* g_SAFInputLayer;
    void EnableSAFInputLock();
    void DisableSAFInputLock();

    // Camera globals
    extern RE::NiCamera* g_NiCamera;
    extern RE::NiAVObject* g_CameraRoot;
    extern RE::NiPoint3 g_PrevRootLocal;
    extern RE::NiPoint3 g_PrevRootWorld;
    extern RE::NiMatrix3 g_PrevRootLocalRot;
    extern RE::NiMatrix3 g_PrevRootWorldRot;
    extern RE::NiMatrix3 g_PrevRootPrevWorldRot;
    extern bool g_RestoreRootRotation;
    extern bool g_FurnitureYawLocked;
    extern float g_FurnitureBaseYaw;
    extern float g_FurnitureSavedBodyYaw;
    extern bool g_FurnitureYawRestored;
    extern RE::NiPoint3 g_PrevSetLocal;
    extern RE::NiPoint3 g_PrevSetWorld;
    extern int g_PrevSetWorldFrame;
    extern int g_FurnitureGraceFrames;
    extern float g_AngleBlendZ;
    extern int g_FrameCount;

    // Math helpers
    RE::NiPoint3 TransformLocalToWorld(const RE::NiMatrix3& rotation, const RE::NiPoint3& localOffset);
    RE::NiPoint3 TransformWorldToLocal(const RE::NiMatrix3& rotation, const RE::NiPoint3& worldOffset);
    RE::NiPoint3 ComputeNodeLocalFromWorld(RE::NiAVObject* node, const RE::NiPoint3& worldPos);
    RE::NiPoint3 ComputeNodeWorldFromLocal(RE::NiAVObject* node, const RE::NiPoint3& localPos);
    RE::NiMatrix3 ComputeNodeLocalRotationFromWorld(RE::NiAVObject* node, const RE::NiMatrix3& desiredWorldRot);
    float ClampFloat(float value, float minValue, float maxValue);
    float WrapAnglePi(float angle);
    float DistanceSquared(const RE::NiPoint3& a, const RE::NiPoint3& b);
    bool IsFinitePoint(const RE::NiPoint3& point);

    template <typename F>
    inline void VisitNodesByName(RE::NiAVObject* root, const char* nameSubstr, F&& callback)
    {
        if (!root) return;
        const char* nameStr = root->name.c_str();
        if (nameStr && std::strstr(nameStr, nameSubstr)) {
            callback(root);
        }
        auto* asNode = root->GetAsNiNode();
        if (asNode) {
            for (auto& child : asNode->children) {
                if (child) {
                    VisitNodesByName(child.get(), nameSubstr, callback);
                }
            }
        }
    }
    bool NormalizePlanar(RE::NiPoint3& vector);
    bool Normalize3D(RE::NiPoint3& vector);
    void GetYawAxes(float yaw, RE::NiPoint3& outForward, RE::NiPoint3& outRight);
    RE::NiPoint3 CrossProduct(const RE::NiPoint3& a, const RE::NiPoint3& b);
    RE::NiPoint3 GetHeadMeshCenter(RE::NiAVObject* headMesh);
    void GetSAFStableHeadFrame(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp);

    // Logging
    void LogFormatted(const char* fmt, ...);

    // Functions
    RE::NiCamera* FindNiCamera(RE::TESCamera* tesCam);
    RE::NiAVObject* GetCachedHeadBone();

    void InitEyeHeight();
    void RefreshHeadBone();
    void HideHead(bool a_hide);
    void RestoreCameraOrbit();
    void ResetPseudoFPPState(bool a_restoreOrbit = true);
    void ResetCameraNodesToPlayer();
    void ClearPseudoCameraPointers();
    // Proper manual-toggle-off path: restores the camera to its true
    // pre-pseudo transform (or, failing that, snaps it to the player's
    // real position) instead of baking in whatever position pseudo last
    // wrote. Use this instead of calling ResetPseudoFPPState() directly
    // from a user-initiated disable (F4/F8/etc). See PseudoFP.cpp's
    // g_SavedRootLocal/g_SavedNiCamLocal/g_SavedTransformsValid.
    void FullyResetPseudoState();
    void DisablePseudoFPPAndRestoreCamera();

    bool ComputePseudoFPPWorldPosition(RE::TESCamera* tesCam, RE::NiPoint3& outWorldPos,
        RE::NiPoint3* outHeadAnchor = nullptr, bool* outUsingFallback = nullptr);

    bool IsPlayerUsingFurniture(RE::PlayerCharacter* player);
    bool IsInVehicleCameraState(RE::PlayerCamera* camera);
    bool IsInFreeCameraState(RE::PlayerCamera* camera);

    // Saved clean camera transforms for flight transition restore.
    // Saved in ApplyPseudoFPPRig before any modifications, restored in
    // DetourSetCameraState when a ship flight state is entered, so the
    // FlightCameraState initialises from the game's kFurniture transforms
    // rather than the player-head position the pseudo rig wrote.
    extern bool g_HasFlightSave;
    extern RE::NiTransform g_SavedCRLocal;
    extern RE::NiTransform g_SavedCRWorld;
    extern RE::NiPoint3 g_SavedNiCamLocal;
    extern RE::NiPoint3 g_SavedNiCamWorld;
}
