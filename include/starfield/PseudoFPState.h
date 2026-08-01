/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <cstdint>
#include <chrono>
#include "RE/N/NiAVObject.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiPoint.h"
#include "RE/T/TESCamera.h"
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/B/BSInputEnableLayer.h"
#include "starfield/ShipCamera.h"

namespace Patch {

    // === Global flags ===
    extern bool g_PseudoFPPActive;
    extern bool g_SAFAnimationPlaying;

    // === Seat-exit grace ===
    // Set by EventsStarfield.cpp when the player transitions out of the
    // ship pilot seat (true->false). The base-game pilot-seat stand-up
    // is NOT an SAF animation and the camera may stay in a ship state
    // for its whole duration, so without this grace shouldDisablePseudo
    // and the IsInShipCameraState guards in PseudoFP.cpp would both fire
    // and the stand-up would play in vanilla TPP. Read by PseudoFP.cpp
    // to relax its ship-state bails for ~2s after a seat exit.
    bool IsSeatExitPseudoGraceActive();

    // === Head bone tracking ===
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
    extern int g_FrameCount;

    // === SAF rigid rotation lock ===
    // While a SAF animation plays we disable player movement input so the
    // actor is rooted and stays glued to the animated body.
    extern RE::BSInputEnableLayer* g_SAFInputLayer;
    void EnableSAFInputLock();
    void DisableSAFInputLock();

    // === Camera state ===
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

    // === ForceCameraToHead cache ===
    extern int g_LastForceFrame;
    extern RE::NiPoint3 g_CachedForcePosition;
    extern RE::NiMatrix3 g_CachedForceRotation;
    extern bool g_CachedForceRotationValid;

    // === Smooth camera-position transition (F4 toggle / native camera
    // switch such as "/") ===
    // Instead of hard-cutting cameraRoot/NiCamera translate straight to
    // the new target every time pseudo activates or deactivates, we blend
    // from wherever the camera actually was at the instant of the switch
    // to the target position over a short, configurable window. This is
    // purely a position blend (rotation stays whatever mouse-look/engine
    // already has it at) so it never fights player look input.
    extern bool g_PosTransitionActive;
    extern RE::NiPoint3 g_PosTransitionStartWorld;
    extern std::chrono::steady_clock::time_point g_PosTransitionStartTime;
    void StartPosTransition(const RE::NiPoint3& fromWorldPos);
    // Returns the blended position for this frame given the true target
    // position; updates/clears g_PosTransitionActive as the blend completes.
    RE::NiPoint3 ApplyPosTransition(const RE::NiPoint3& targetWorldPos);

    // Set once per real frame by the EventsStarfield permanent task; read
    // by ComputePseudoFPPWorldPosition to avoid re-querying the SAF DLL.
    extern bool g_SAFPlayingRawCached;

    // === Math helpers ===
    RE::NiPoint3 TransformLocalToWorld(const RE::NiMatrix3& rotation, const RE::NiPoint3& localOffset);
    RE::NiPoint3 TransformWorldToLocal(const RE::NiMatrix3& rotation, const RE::NiPoint3& worldOffset);
    RE::NiPoint3 ComputeNodeLocalFromWorld(RE::NiAVObject* node, const RE::NiPoint3& desiredWorldPos);
    RE::NiMatrix3 ComputeNodeLocalRotationFromWorld(RE::NiAVObject* node, const RE::NiMatrix3& desiredWorldRot);
    float ClampFloat(float value, float minValue, float maxValue);
    float WrapAnglePi(float angle);
    RE::NiMatrix3 MakeYawMatrix(float yaw);
    float DistanceSquared(const RE::NiPoint3& a, const RE::NiPoint3& b);
    bool IsFinitePoint(const RE::NiPoint3& point);
    bool NormalizePlanar(RE::NiPoint3& vector);
    bool Normalize3D(RE::NiPoint3& vector);
    void GetYawAxes(float yaw, RE::NiPoint3& outForward, RE::NiPoint3& outRight);
    void GetPlayerWorldAxes(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight);
    void GetPseudoFPAxes(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight);
    void GetNodeAxes(RE::NiAVObject* node, RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp);
    void GetPlayerBodyAxes3D(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp);
    RE::NiPoint3 CrossProduct(const RE::NiPoint3& a, const RE::NiPoint3& b);

    // === Head tracking ===
    RE::NiCamera* FindNiCamera(RE::TESCamera* tesCam);
    RE::NiAVObject* GetCachedHeadBone();
    void InitEyeHeight();
    void RefreshHeadBone();
    void HideHead(bool a_hide);
    void RestoreCameraOrbit();
    void ResetCameraNodesToPlayer();
    void ClearPseudoCameraPointers();
    RE::NiPoint3 GetCurrentHeadAnchorWorldPosition();
    void UpdateHeadAnchorData();
    RE::NiPoint3 GetSAFHeadLocalOffset();
    RE::NiPoint3 BuildSAFPinnedCameraLocalOffset();
    void GetSAFStableHeadFrame(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp);
    bool ComputeSAFCameraModeWorldPosition(RE::PlayerCharacter* player, RE::NiPoint3& outAnchor, RE::NiPoint3& outWorldPos);
    RE::NiPoint3 ComputeSAFHeadAnchorWorldPosition(RE::PlayerCharacter* player);
    RE::NiPoint3 ComputeUltraRigidHeadAnchorWorldPosition(RE::PlayerCharacter* player);
    RE::NiPoint3 ComputeUltraRigidCameraWorldPosition(RE::PlayerCharacter* player, const RE::NiPoint3& headAnchor, RE::NiAVObject* cr = nullptr);
    RE::NiPoint3 ComputeSAFRigidEyeWorldPosition(RE::PlayerCharacter* player);
    RE::NiMatrix3 ComputeSAFRigidEyeWorldRotation();
    float ComputeSAFHeadPitchRadians();
    RE::NiMatrix3 BuildYawPreservingPitchRotation(const RE::NiMatrix3& currentRot, float targetPitchRad);
    RE::NiPoint3 ClampWorldPosToPlayerCapsule(RE::PlayerCharacter* player, const RE::NiPoint3& desiredWorldPos, const RE::NiPoint3& headAnchor);
    bool IsReasonableHeadAnchor(RE::PlayerCharacter* player, const RE::NiPoint3& anchorPos);
    bool IsPlausibleHeadAnchor(RE::PlayerCharacter* player, const RE::NiPoint3& anchorPos);
    RE::NiPoint3 GetNodeCenter(RE::NiAVObject* node);
    RE::NiAVObject* FindPreferredEyeNode(RE::NiAVObject* root);
    using NodeVisitorFn = void (*)(RE::NiAVObject*);
    void VisitNodesByName(RE::NiAVObject* root, const char* nameSubstr, NodeVisitorFn callback);

    // === Detection ===
    bool IsPlayerUsingFurniture(RE::PlayerCharacter* player);
    bool IsInVehicleCameraState(RE::PlayerCamera* camera);
    bool IsInFreeCameraState(RE::PlayerCamera* camera);
    void ApplySAFRotationSync(RE::NiAVObject* cr, RE::NiAVObject* niCam, bool recompute);
    void RestorePseudoRig(RE::NiAVObject* a_this);

    // === Logging ===
    void LogFormatted(const char* fmt, ...);
}
