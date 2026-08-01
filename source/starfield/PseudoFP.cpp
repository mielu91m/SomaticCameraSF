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

#include "starfield/PseudoFP.h"
#include "starfield/PseudoFPState.h"
#include "starfield/HeadTracking.h"
#include "starfield/SAFIntegration.h"
#include "starfield/EventsStarfield.h"
#include "systems/PseudoFPConfig.h"
#include "plugin.h"
#include <chrono>
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESCamera.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiNode.h"
#include "RE/B/BGSKeywordForm.h"
#include "RE/B/BSFixedString.h"
#include "RE/IDs_VTABLE.h"
#include <MinHook.h>
#include <cmath>

namespace Patch {

    void (*origUpdate)(RE::TESCamera*) = nullptr;
    void (*origTPSUpdate)(void*) = nullptr;
    void* g_origFPSUpdate = nullptr;
    void* g_origSetCameraState = nullptr;

    void* g_origNiNodeUWD = nullptr;
    void* g_origNiNodeUTB = nullptr;
    void* g_origNiNodeUT = nullptr;
    bool g_NiNodeHooksInstalled = false;

    void* g_origUpdateWorldData = nullptr;
    void* g_origUpdateTransformAndBounds = nullptr;
    void* g_origUpdateTransforms = nullptr;

    RE::NiPoint3 g_SavedRootLocal = {};
    RE::NiPoint3 g_SavedNiCamLocal = {};
    bool g_SavedTransformsValid = false;

     int g_LastForceFrame = -1;
     int g_PostForceSettle = 0;

     // Storage for g_ColdStartSettleFrames (declared extern in PseudoFP.h).
     // In the source project this was ported from, the actual definition
     // lives in EventsStarfield.cpp (which manages the cold-start/reload
     // detection heuristic and decrements this each frame). That file was
     // intentionally left untouched by this SAF/furniture/vehicle port, so
     // EventsStarfield.cpp here never sets this above 0 - DetourUpdate's
     // "if (g_ColdStartSettleFrames > 0) return;" check is effectively
     // always false, i.e. inert, until EventsStarfield.cpp is updated to
     // actually manage it (see the cold-start reload-detection fix from
     // our other conversation, if you want that behavior here too).
int g_ColdStartSettleFrames = 0;
    int g_FramesSinceShipCameraState = 0;
    bool g_PseudoFPPInShipActive = false;
    int g_PseudoFPPShipExitGraceFrames = 0;
    std::chrono::steady_clock::time_point g_LastForceComputeTime{};
    bool g_HasLastForceComputeTime = false;
    RE::NiPoint3 g_CachedForcePosition = {};
    RE::NiMatrix3 g_CachedForceRotation = {};
    bool g_CachedForceRotationValid = false;

    static constexpr int kShipExitPseudoGraceFrames = 120;

    static bool IsShipExitPseudoGraceActive()
    {
        return g_PseudoFPPShipExitGraceFrames > 0;
    }

    static bool IsShipTransitionPseudoAllowed()
    {
        return g_SAFAnimationPlaying || IsShipExitPseudoGraceActive() || IsSeatExitPseudoGraceActive();
    }

    // Rotation captured the instant SAF starts, BEFORE we ever touch
    // cr->world.rotate. This exists because reading "the current camera
    // rotation" to preserve yaw is only safe if that rotation is still
    // genuinely coming from the engine's own mouse-look. Once we start
    // writing our own pitch-synced rotation every frame, cr->world.rotate
    // IS our own last output - feeding that back in as "current" the next
    // frame creates a feedback loop with no connection to the real camera
    // state any more. If the value we happened to read on the very first
    // SAF frame was itself a transitional one (e.g. mid camera-state
    // switch into the SAF cutscene), that wrong yaw got permanently
    // "frozen in" - explaining both the 90 deg snap at scene start and the
    // camera never returning to normal afterward.
    //
    // Fix: snapshot the true rotation once at the SAF start -> use ONLY
    // that snapshot as the yaw source for the whole scene (yaw is
    // therefore fixed for the duration of the scene; only pitch keeps
    // updating from the head bone) -> explicitly restore that exact
    // snapshot when SAF ends, instead of leaving whatever we last forced.
    bool g_HasPreSAFRotation = false;
    RE::NiMatrix3 g_PreSAFRotation = {};

    void ApplySAFRotationSync(RE::NiAVObject* cr, RE::NiAVObject* niCam, bool recompute)
    {
        static bool s_wasSAFActive = false;

        if (!cr) {
            return;
        }

        const bool pitchSyncEnabled = Systems::PseudoFPConfigManager::Get().IsSAFPitchSyncEnabled();

        if (!g_SAFAnimationPlaying) {
            g_CachedForceRotationValid = false;
            if (pitchSyncEnabled && s_wasSAFActive && g_HasPreSAFRotation) {
                // SAF just ended: put the camera back exactly how it was
                // before the scene, rather than leaving it at the last
                // forced pitch/frozen-yaw value.
                const RE::NiMatrix3& restoreRot = g_PreSAFRotation;
                cr->local.rotate = ComputeNodeLocalRotationFromWorld(cr, restoreRot);
                cr->previousWorld.rotate = restoreRot;
                cr->world.rotate = restoreRot;
                if (niCam) {
                    RE::NiMatrix3 identity{};
                    identity.entry[0][0] = 1.0f;
                    identity.entry[1][1] = 1.0f;
                    identity.entry[2][2] = 1.0f;
                    niCam->local.rotate = identity;
                    niCam->previousWorld.rotate = restoreRot;
                    niCam->world.rotate = restoreRot;
                }
            }
            s_wasSAFActive = false;
            g_HasPreSAFRotation = false;
            return;
        }

        if (!s_wasSAFActive) {
            // SAF just started: grab the real, engine-driven rotation
            // ONE time before we ever overwrite it. Logged in full (raw
            // 3x3, all 9 entries) regardless of whether pitch-sync is
            // enabled, so we have real numbers to check the camera's
            // actual axis convention against next time - see
            // SAF_ROT_RAW below.
            g_PreSAFRotation = cr->world.rotate;
            g_HasPreSAFRotation = true;
            g_CachedForceRotationValid = false;
        }
        s_wasSAFActive = true;

        if (!pitchSyncEnabled) {
            // Feature disabled (default): leave rotation alone entirely,
            // same as before any of the pitch-sync attempts. Position 1:1
            // head tracking (handled elsewhere in this file) is unaffected.
            return;
        }

        if (recompute || !g_CachedForceRotationValid) {
            const float headPitch = ComputeSAFHeadPitchRadians();
            // Yaw always comes from the frozen pre-SAF snapshot, never
            // from cr->world.rotate (which is our own output by now) -
            // this is what breaks the feedback loop. Yaw is therefore
            // fixed for the duration of the scene; only pitch follows the
            // head bone.
            g_CachedForceRotation = BuildYawPreservingPitchRotation(g_PreSAFRotation, headPitch);
            g_CachedForceRotationValid = true;
        }
        const RE::NiMatrix3& syncedRot = g_CachedForceRotation;
        cr->local.rotate = ComputeNodeLocalRotationFromWorld(cr, syncedRot);
        cr->previousWorld.rotate = syncedRot;
        cr->world.rotate = syncedRot;
        if (niCam) {
            // niCam sits with zero local offset/rotation relative to cr
            // (same reasoning as niCam->local.translate = {0,0,0}
            // elsewhere), so its world rotation should match cr's exactly.
            RE::NiMatrix3 identity{};
            identity.entry[0][0] = 1.0f;
            identity.entry[1][1] = 1.0f;
            identity.entry[2][2] = 1.0f;
            niCam->local.rotate = identity;
            niCam->previousWorld.rotate = syncedRot;
            niCam->world.rotate = syncedRot;
        }
    }

    bool ApplyPseudoFPPRig(RE::TESCamera* tesCam, void* tpsThis)
    {
        // Hand off to the engine's own camera during ship flight states
        // (kFlight, kShipAction, etc.) so the pilot can fly with the
        // standard camera.  During the pilot seat sit/stand animation
        // the engine also needs full control of the camera view.
        auto* camera = RE::PlayerCamera::GetSingleton();
        if (camera && IsInShipCameraState(camera) && !IsSeatExitPseudoGraceActive()) {
            return true;
        }
        auto* playerCheck = RE::PlayerCharacter::GetSingleton();
        if (playerCheck && IsPlayerInShipPilotSeat(playerCheck) && !IsSeatExitPseudoGraceActive()) {
            return true;
        }
        if (camera && !camera->IsInThirdPerson() && !camera->QCameraEquals(RE::CameraState::kIronSights) && !IsInShipCameraState(camera) && !IsInVehicleCameraState(camera)) {
            auto* tcam = static_cast<RE::TESCamera*>(camera);
            uint32_t currentIdx = 0xFF;
            for (uint32_t i = 0; i < RE::CameraState::kTotal; i++) {
                if (tcam->currentState == camera->cameraStates[i]) {
                    currentIdx = i;
                    break;
                }
            }

        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* niCam = FindNiCamera(tesCam);
        if (!player || !niCam || !tesCam) return false;
        auto* cr = tesCam->cameraRoot.get();
        if (!cr) return false;
        g_CameraRoot = cr;
        g_NiCamera = niCam;

        bool usingFallback = false;
        RE::NiPoint3 headAnchor;
        RE::NiPoint3 worldPos;
        if (!ComputePseudoFPPWorldPosition(tesCam, worldPos, &headAnchor, &usingFallback)) {
            return false;
        }

        // Ship-relative head correction: DISABLED due to crashes
        // GetSpaceship/IsSpaceshipLanded/IsSpaceshipDocked crash during
        // seat transition. Using camera state only instead.

        constexpr bool kEnableBodyFollowsCameraYaw = true;
        constexpr float kMaxYawTurnPerFrameDeg = 6.0f;
        float camYaw = std::atan2(cr->world.rotate.entry[1][0], cr->world.rotate.entry[0][0]);
        float bodyYaw = player->GetAngleZ();
        const bool isFurniture = IsPlayerUsingFurniture(player) || (camera && camera->QCameraEquals(RE::CameraState::kFurniture));
        // Ship pilot seat sets occupiedFurniture just like any chair, so it
        // would otherwise fall into the isFurniture branch below - but that
        // branch's yaw handling (locking to the furniture's own rotation,
        // then restoring the player's saved yaw on stand-up) was tuned for
        // open-room chairs and doesn't match the ship's tight cockpit
        // geometry: it made the stand-up animation walk forward into the
        // dashboard instead of stepping back like vanilla. Must be excluded
        // here the same way ship flight already is, so the seat's own
        // native sit/stand animation and yaw are left completely alone.
        const bool inShipPilotSeat = player && IsPlayerInShipPilotSeat(player);
        g_RestoreRootRotation = false;

        if (inShipPilotSeat) {
            // Sitting in / standing up from the pilot seat: don't touch
            // yaw at all, for either direction - let the game's own seat
            // marker animation and orientation run untouched. Position
            // still gets head-anchored below via the shared translate-only
            // block, exactly like ship flight.
        } else if (camera && IsInVehicleCameraState(camera)) {
            if (!g_FurnitureYawLocked) {
                g_FurnitureBaseYaw = bodyYaw;
                g_FurnitureYawLocked = true;
            }
            player->data.angle.z = g_FurnitureBaseYaw;
        } else if (isFurniture) {
            // Orientation of the player while sitting/standing is left
            // entirely to the vanilla engine: the furniture sit/stand
            // marker animation owns the entry/exit direction, so we must
            // NOT overwrite player->data.angle.z here. Overriding it was
            // what made the player face into / walk through the furniture
            // on exit. The pseudo rig below only repositions the camera.
            g_FurnitureGraceFrames = static_cast<int>(Systems::PseudoFPConfigManager::Get().GetFurnitureExitGraceFrames());
        } else {
            if (g_FurnitureGraceFrames > 0) {
                g_FurnitureGraceFrames--;
            } else {
                g_FurnitureYawLocked = false;
                if (kEnableBodyFollowsCameraYaw && camera && camera->IsInThirdPerson()) {
                    float diff = WrapAnglePi(camYaw - bodyYaw);
                    float maxYaw = Systems::PseudoFPConfigManager::Get().GetMaxYawRad();
                    if (std::fabs(diff) > maxYaw) {
                        float clamped = ClampFloat(diff, -maxYaw, maxYaw);
                        float correction = diff - clamped;
                        float maxStep = kMaxYawTurnPerFrameDeg * (3.14159265f / 180.0f);
                        float step = ClampFloat(correction, -maxStep, maxStep);
                        if (std::fabs(step) > 0.0001f) {
                            player->data.angle.z = bodyYaw + step;
                        }
                    }
                }
            }
        }

        RE::NiPoint3 rootLocal = {};
        if (!g_SavedTransformsValid) {
            g_SavedRootLocal = cr->local.translate;
            g_SavedNiCamLocal = niCam->local.translate;
            g_SavedTransformsValid = true;
            // This is the exact edge where the rig switches on (F4 toggle,
            // SAF start, ADS end, or coming back from a native camera-state
            // switch such as "/"): cr/niCam still hold whatever the
            // vanilla/previous camera position was, one line before we
            // overwrite them with the head-anchored position below. Arm the
            // blend from there so the cut to the head position eases in
            // instead of popping.
            StartPosTransition(niCam->world.translate);
        }

        // Normal pseudo: position camera at head bone. If a transition is
        // in flight (just activated pseudo, or came back from a native
        // camera-state switch), blend toward worldPos instead of snapping
        // straight to it so the cut doesn't read as a hard pop/jitter.
        const RE::NiPoint3 blendedWorldPos = ApplyPosTransition(worldPos);
        rootLocal = ComputeNodeLocalFromWorld(cr, blendedWorldPos);
        cr->local.translate = rootLocal;
        cr->previousWorld.translate = blendedWorldPos;
        cr->world.translate = blendedWorldPos;

        niCam->local.translate = { 0.0f, 0.0f, 0.0f };
        niCam->previousWorld.translate = blendedWorldPos;
        niCam->world.translate = blendedWorldPos;

        // Sync camera PITCH to the SAF-animated head's actual pitch.
        // Position alone being pinned to the head bone (above) isn't
        // enough: rotation is otherwise left exactly as mouse-look last
        // set it, which has no idea the whole body just rotated ~90 deg
        // to lie down. That mismatch is what makes the camera look like
        // it's sitting behind the head (prone) or in front of the face
        // (supine) even though its POSITION is exactly at the head bone.
        // Yaw is deliberately left untouched (still mouse-controlled, so
        // players can freely look left/right during the scene); only the
        // up/down look is replaced with the head's own elevation angle.
        ApplySAFRotationSync(cr, niCam, /*recompute=*/true);

        const RE::NiPoint3 actualWorldPos = blendedWorldPos;
        g_PrevRootLocal = rootLocal;
        g_PrevRootWorld = actualWorldPos;
        g_PrevRootLocalRot = cr->local.rotate;
        g_PrevRootWorldRot = cr->world.rotate;
        g_PrevRootPrevWorldRot = cr->previousWorld.rotate;
        g_PrevSetLocal = {};
        g_PrevSetWorld = actualWorldPos;
        g_PrevSetWorldFrame = g_FrameCount;

        if (tpsThis) {
            float* ptr = reinterpret_cast<float*>((uintptr_t)tpsThis + 0x1A8);
            ptr[0] = 0.0f;
            ptr[1] = 0.0f;
        }

        g_FrameCount++;
        g_CachedForcePosition = actualWorldPos;
        g_LastForceFrame = g_FrameCount;
        g_LastForceComputeTime = std::chrono::steady_clock::now();
        g_HasLastForceComputeTime = true;
        if ((g_FrameCount % 300) == 0) {
            auto guard = player->loadedData.LockRead();
            auto* loaded = *guard;
            RE::NiPoint3 rootPos = {};
            if (loaded && loaded->data3D.get())
                rootPos = loaded->data3D->world.translate;
            RE::NiPoint3 headW = {};
            RE::NiPoint3 headNode = {};
            if (g_HeadMesh)
                headW = GetHeadMeshCenter(g_HeadMesh);
            if (g_HeadAnchorNode)
                headNode = g_HeadAnchorNode->world.translate;

        }
        return true;
    }

    void ForceCameraToHead()
    {
        // During SAF stand-up/sit-down from pilot seat, allow pseudo rig
        // even in FPP-in-ship context so stand-up/sit animations use
        // pseudo camera (not vanilla TPP).
        // Note: IsPlayerInShipPilotSeat is NOT checked here because the
        // player leaves the seat immediately when stand-up starts.
        if (g_PseudoFPPInShipActive && !IsShipTransitionPseudoAllowed()) {
            if (!g_SAFAnimationPlaying) return;
        }
        // Free camera states (photo mode, free fly): do not override the
        // engine's free camera position. This guard covers ALL callers
        // including NiNode hooks (RestorePseudoRig -> ForceCameraToHead
        // and ForceCameraToHeadPostSkeleton -> ForceCameraToHead) which
        // fire on every skeleton/camera node update regardless of what
        // DetourUpdate or the permanent-task pin check.
        {
            auto* cam = RE::PlayerCamera::GetSingleton();
            if (cam && IsInFreeCameraState(cam)) {
                return;
            }
        }
        // Ship flight (and FPP-in-ship grace period): hand off to the
        // engine's own camera. Pilot seat without flight is handled
        // normally (same as furniture).
        {
            auto* cam = RE::PlayerCamera::GetSingleton();
            if (cam && IsInShipCameraState(cam) && !IsSeatExitPseudoGraceActive()) {
                g_FramesSinceShipCameraState = 0;
                return;
            }
            if (!IsShipTransitionPseudoAllowed() && cam && g_FramesSinceShipCameraState <= 360) {
                return;
            }
        }
        // FPP in spaceship pilot seat: ship camera system handles
        // head tracking; applying pseudo head position corrupts FPP.
        {
            auto* cam = RE::PlayerCamera::GetSingleton();
            if (cam && cam->IsInFirstPerson()) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player && IsPlayerInShipPilotSeat(player)) return;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const bool sameRealFrame = g_HasLastForceComputeTime &&
            std::chrono::duration_cast<std::chrono::microseconds>(now - g_LastForceComputeTime).count() < 2000;
        if (sameRealFrame) {
            auto* camera = RE::PlayerCamera::GetSingleton();
            auto* tesCam = camera ? static_cast<RE::TESCamera*>(camera) : nullptr;
            if (tesCam) {
                auto* cr = tesCam->cameraRoot.get();
                auto* niCam = FindNiCamera(tesCam);
                if (cr) {
                    cr->local.translate = ComputeNodeLocalFromWorld(cr, g_CachedForcePosition);
                    cr->previousWorld.translate = g_CachedForcePosition;
                    cr->world.translate = g_CachedForcePosition;
                }
                if (niCam) {
                    niCam->local.translate = {};
                    niCam->previousWorld.translate = g_CachedForcePosition;
                    niCam->world.translate = g_CachedForcePosition;
                }
                ApplySAFRotationSync(cr, niCam, /*recompute=*/false);
            }
            return;
        }
        g_LastForceComputeTime = now;
        g_HasLastForceComputeTime = true;

        auto* camera = RE::PlayerCamera::GetSingleton();
        auto* tesCam = camera ? static_cast<RE::TESCamera*>(camera) : nullptr;
        if (!tesCam) return;
        auto* cr = tesCam->cameraRoot.get();
        auto* niCam = FindNiCamera(tesCam);
        if (!cr && !niCam) {
            return;
        }

        RE::NiPoint3 freshWorld;
        bool usingFallback = false;
        if (!ComputePseudoFPPWorldPosition(tesCam, freshWorld, nullptr, &usingFallback)) {
            return;
        }

        // Same blend as ApplyPseudoFPPRig - if a transition is in flight,
        // ease toward freshWorld instead of snapping. Cache the blended
        // value (not the raw target) so the sameRealFrame fast-path above
        // reuses exactly what was actually written this frame.
        g_CachedForcePosition = ApplyPosTransition(freshWorld);

        if (cr) {
            cr->local.translate = ComputeNodeLocalFromWorld(cr, g_CachedForcePosition);
            cr->previousWorld.translate = g_CachedForcePosition;
            cr->world.translate = g_CachedForcePosition;
        }
        if (niCam) {
            niCam->local.translate = {};
            niCam->previousWorld.translate = g_CachedForcePosition;
            niCam->world.translate = g_CachedForcePosition;
        }
        ApplySAFRotationSync(cr, niCam, /*recompute=*/true);
    }

    void RestorePseudoRig(RE::NiAVObject* a_this)
    {
        if (!g_PseudoFPPActive) return;
        // During SAF stand-up from pilot seat, allow pseudo rig
        // even in FPP-in-ship context so the stand-up animation
        // stays in pseudo camera (not vanilla TPP).
        // Note: we do NOT check IsPlayerInShipPilotSeat here because
        // the player leaves the seat immediately when stand-up starts,
        // before g_SAFAnimationPlaying turns false — the check would
        // block pseudo mid-animation. If FPP-in-ship + SAF, it is
        // always a pilot-seat animation regardless of camera state.
        // During SAF we allow pseudo in any camera state (FPP or TPP).
        if (g_PseudoFPPInShipActive && !IsShipTransitionPseudoAllowed()) {
            if (!g_SAFAnimationPlaying) return;
        }

        // Ship camera states: do not override the engine's ship camera
        // positioning. Also track a grace period (up to ~2 seconds at
        // 60fps) after leaving a ship camera state — SpaceshipFirstPersonState
        // (FPP while flying) isn't a ship camera state, so Without this
        // grace the pseudo rig would still hijack FPP-in-ship.
        {
            auto* cam = RE::PlayerCamera::GetSingleton();
            if (cam && IsInShipCameraState(cam) && !IsSeatExitPseudoGraceActive()) {
                g_FramesSinceShipCameraState = 0;
                return;
            }
            if (!IsShipTransitionPseudoAllowed() && g_FramesSinceShipCameraState <= 360) {
                g_FramesSinceShipCameraState++;
                return;
            }
        }

        auto* camera = RE::PlayerCamera::GetSingleton();
        // Normal FPP (no SAF): engine handles head tracking → skip.
        // During SAF in spaceship pilot seat: normally the ship's camera
        // system handles FPP head positioning correctly, so we'd skip
        // pseudo. BUT during FPP-in-ship (g_PseudoFPPInShipActive),
        // the ship camera handles cockpit view natively — we still want
        // pseudo active for stand-up/sit-down SAF animations so the
        // camera stays in pseudo (not vanilla TPP) during the transition.
        if (camera && camera->IsInFirstPerson()) {
            if (!g_SAFAnimationPlaying) return;
            auto* player = RE::PlayerCharacter::GetSingleton();
            // Only skip pseudo for SAF in pilot seat when NOT in FPP-in-ship
            // context. When g_PseudoFPPInShipActive is true, we need
            // RestorePseudoRig to apply the pseudo rig for stand-up/sit-down.
            if (player && IsPlayerInShipPilotSeat(player) && !g_PseudoFPPInShipActive) return;
        }

        // Fast path: cache is populated - resolve by pointer comparison
        // only, no tree walk, REGARDLESS of whether a_this matches.
        //
        // This function runs from the NiNode hooks, which fire for EVERY
        // NiNode update in the ENTIRE scene - every bone of every NPC,
        // every prop, every piece of furniture - thousands of times per
        // frame. The previous version only skipped the walk below when
        // a_this actually WAS the camera; for every other node (i.e.
        // almost every single call) it fell straight through into the
        // "slow path" and ran a full FindNiCamera() tree walk anyway.
        // That's thousands of tree walks per frame instead of zero - the
        // real source of the FPS drop/stutter, and very likely also the
        // NPC lip-sync hitching during dialogue: dialogue scenes bring
        // extra NPCs into close-up view, meaning extra skeleton nodes and
        // therefore extra walks, causing frame hitches during exactly the
        // scenes where facial-animation timing matters most.
        if (g_NiCamera && g_CameraRoot) {
            if (a_this == g_NiCamera || a_this == g_CameraRoot) {
                ForceCameraToHead();
            }
            return;
        }

        // Slow path: cache is genuinely empty (first activation, or
        // invalidated elsewhere by a skeleton/camera-state change) -
        // re-resolve once and refill the cache so every subsequent call
        // this session takes the fast path above instead of repeating
        // this walk.
        {
            auto* tesCam = camera ? static_cast<RE::TESCamera*>(camera) : nullptr;
            if (!tesCam) return;
            auto* currentNiCamera = FindNiCamera(tesCam);
            auto* currentCameraRoot = tesCam->cameraRoot.get();
            if (!currentNiCamera && !currentCameraRoot) return;
            if (currentNiCamera) g_NiCamera = currentNiCamera;
            if (currentCameraRoot) g_CameraRoot = currentCameraRoot;
            if (a_this != currentNiCamera && a_this != currentCameraRoot) return;
        }

        ForceCameraToHead();
    }

    // Saved hook target addresses for dynamic enable/disable.
    static void* g_HookedUwdAddr = nullptr;
    static void* g_HookedUtbAddr = nullptr;
    static void* g_HookedUtAddr = nullptr;

    // Tracks what SetNiNodeHooksActive() last set, independent of MinHook's
    // own internal state, so EnsureVanillaSafetyNet() below can detect a
    // desync (g_PseudoFPPActive says "off" but the hooks were never told to
    // disable) without needing a MinHook query API.
    static bool g_NiNodeHooksCurrentlyActive = false;

    void SetNiNodeHooksActive(bool active)
    {
        if (g_HookedUwdAddr) {
            if (active) MH_EnableHook(g_HookedUwdAddr);
            else        MH_DisableHook(g_HookedUwdAddr);
        }
        if (g_HookedUtbAddr) {
            if (active) MH_EnableHook(g_HookedUtbAddr);
            else        MH_DisableHook(g_HookedUtbAddr);
        }
        if (g_HookedUtAddr) {
            if (active) MH_EnableHook(g_HookedUtAddr);
            else        MH_DisableHook(g_HookedUtAddr);
        }
        g_NiNodeHooksCurrentlyActive = active;
    }

    // SAFETY NET (belt-and-suspenders): every place in this codebase that
    // sets g_PseudoFPPActive = false has been re-audited to also call
    // SetNiNodeHooksActive(false) in the same breath, so this should never
    // actually fire. It exists purely as a self-healing backstop: if
    // g_PseudoFPPActive and the NiNode hooks' actual last-set state ever
    // disagree - a future code change that forgets the pairing, an
    // exception/AV mid-toggle that skips the disable call, or any other
    // edge case not yet found - this forces them back in sync on the very
    // next frame instead of silently running mismatched for the rest of
    // the session (which is exactly the "vanilla behaves like pseudo is
    // still holding onto something" symptom). Logs once (not spammed) so a
    // future debug log will show clearly whether this ever actually fired.
    void EnsureVanillaSafetyNet()
    {
        if (g_PseudoFPPActive != g_NiNodeHooksCurrentlyActive) {
            static bool everMismatched = false;
            if (!everMismatched) {
                everMismatched = true;
                LogFormatted("SAFETY NET: g_PseudoFPPActive=%d but NiNode hooks active state=%d - forcing sync",
                    g_PseudoFPPActive ? 1 : 0, g_NiNodeHooksCurrentlyActive ? 1 : 0);
            }
            SetNiNodeHooksActive(g_PseudoFPPActive);
        }
    }

    void InstallNiNodeHooks()
    {
        if (g_NiNodeHooksInstalled) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto guard = player->loadedData.LockRead();
        auto* loaded = *guard;
        if (!loaded || !loaded->data3D.get()) return;
        auto* skeletonRoot = loaded->data3D.get();
        auto* niNodeVtab = *reinterpret_cast<void**>(skeletonRoot);
        if (!niNodeVtab) return;
        auto avObjVtabAddr = RE::VTABLE::NiAVObject[0].address();
        auto uwAddr = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(niNodeVtab) + 78 * 8);
        auto utbAddr = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(niNodeVtab) + 79 * 8);
        auto utAddr = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(niNodeVtab) + 80 * 8);
        auto avUwAddr = *reinterpret_cast<void**>(avObjVtabAddr + 78 * 8);
        if (uwAddr && uwAddr != avUwAddr) {
            g_HookedUwdAddr = uwAddr;
            MH_CreateHook(uwAddr, (void*)DetourNiNodeUWD, (void**)&g_origNiNodeUWD);
            LogFormatted("NiNode::UpdateWorldData hooked at %p (vtable %p)", uwAddr, niNodeVtab);
        } else if (uwAddr) {
            g_origNiNodeUWD = g_origUpdateWorldData;
        }
        auto avUtbAddr = *reinterpret_cast<void**>(avObjVtabAddr + 79 * 8);
        if (utbAddr && utbAddr != avUtbAddr) {
            g_HookedUtbAddr = utbAddr;
            MH_CreateHook(utbAddr, (void*)DetourNiNodeUTB, (void**)&g_origNiNodeUTB);
            LogFormatted("NiNode::UpdateTransformAndBounds hooked at %p (vtable %p)", utbAddr, niNodeVtab);
        } else if (utbAddr) {
            g_origNiNodeUTB = g_origUpdateTransformAndBounds;
        }
        auto avUtAddr = *reinterpret_cast<void**>(avObjVtabAddr + 80 * 8);
        if (utAddr && utAddr != avUtAddr) {
            g_HookedUtAddr = utAddr;
            MH_CreateHook(utAddr, (void*)DetourNiNodeUT, (void**)&g_origNiNodeUT);
            LogFormatted("NiNode::UpdateTransforms hooked at %p (vtable %p)", utAddr, niNodeVtab);
        } else if (utAddr) {
            g_origNiNodeUT = g_origUpdateTransforms;
        }
        g_NiNodeHooksInstalled = true;
    }

     void DetourUpdate(RE::TESCamera* a_this)
     {
         auto* camera = RE::PlayerCamera::GetSingleton();
         const bool isPlayerCam = (static_cast<RE::TESCamera*>(camera) == a_this);
 
         if (isPlayerCam) {
             if (g_PseudoFPPShipExitGraceFrames > 0) {
                 if (camera && IsInShipCameraState(camera)) {
                     g_PseudoFPPShipExitGraceFrames = 0;
                 } else {
                     g_PseudoFPPShipExitGraceFrames--;
                 }
             }
             InstallNiNodeHooks();
             auto* player = RE::PlayerCharacter::GetSingleton();
             if (player) {
                 static float g_LastPlayerAngleZ = 0.0f;
                 float angleDelta = std::fabs(player->data.angle.z - g_LastPlayerAngleZ);
                 g_LastPlayerAngleZ = player->data.angle.z;
                 constexpr float kTurnThreshold = 0.0005f;
                 constexpr float kTurnMax = 0.02f;
                 if (angleDelta > kTurnThreshold) {
                     g_AngleBlendZ = std::min((angleDelta - kTurnThreshold) / (kTurnMax - kTurnThreshold), 1.0f);
                 } else {
                     g_AngleBlendZ = 0.0f;
                 }
             }
         }
 
         // BUGFIX: DetourUpdate (TESCamera::Update) is also always-on and
         // never disabled - calling into SAFIntegration (a cross-DLL call
         // into StarfieldAnimationFramework.dll) here unconditionally, every
         // frame, regardless of whether pseudo is even toggled on, was pure
         // wasted overhead the rest of the time. Its result is only ever
         // consumed below inside a g_PseudoFPPActive-gated condition.
         auto* playerBeforeUpdate = (isPlayerCam && g_PseudoFPPActive) ? RE::PlayerCharacter::GetSingleton() : nullptr;
         const bool safPlayingBeforeUpdate = playerBeforeUpdate && SAFIntegration::IsSAFAnimationPlaying(playerBeforeUpdate);
 
         if (isPlayerCam && g_PseudoFPPActive &&
             (camera->IsInThirdPerson() ||
              camera->QCameraEquals(RE::CameraState::kFurniture) ||
              camera->QCameraEquals(RE::CameraState::kIronSights) ||
              IsInVehicleCameraState(camera) ||
              safPlayingBeforeUpdate)) {
             auto* niCam = FindNiCamera(a_this);
             if (niCam) {
                 g_NiCamera = niCam;
                 g_CameraRoot = a_this->cameraRoot.get();
             }
         }
 
         origUpdate(a_this);
 
         // During engine settle after a save load, camera pointers
         // (g_NiCamera, g_CameraRoot, g_HeadBone) may still be stale
         // from the previous session. Processing pseudo camera state
         // during this window causes AV crashes in ForceCameraToHead
         // / ApplyPseudoFPPRig. Skip entirely until settled.
         // Also skip pseudo processing for a few frames after
         // ForceThirdPerson() — the engine reinitializes camera
         // structures asynchronously and needs time to stabilize.
         if (g_ColdStartSettleFrames > 0) return;
         if (g_PostForceSettle > 0) {
             g_PostForceSettle--;
             return;
         }
 
           if (!isPlayerCam) return;
if (!g_PseudoFPPActive) return;
        // During FPP-in-ship, only allow pseudo through for SAF stand-up/sit-down
        // in pilot seat so the animation stays in pseudo camera (not vanilla TPP).
        if (g_PseudoFPPInShipActive && !IsShipTransitionPseudoAllowed()) {
            if (!g_SAFAnimationPlaying) return;
        }

// Space flight states must not be processed by pseudo camera
        // transforms. Vanilla handles spaceship camera positioning
        // (FlightCameraState / SpaceshipThirdPersonState) and the
        // TPP→FPP transition uses SpaceshipFirstPersonState.
        {
            auto* cam = RE::PlayerCamera::GetSingleton();
            if (cam && (cam->QCameraEquals(RE::CameraState::kFlight) ||
                        cam->QCameraEquals(RE::CameraState::kShipAction) ||
                        cam->QCameraEquals(RE::CameraState::kShipTargeting) ||
                        cam->QCameraEquals(RE::CameraState::kShipCombatOrbit) ||
                        cam->QCameraEquals(RE::CameraState::kShipFarTravel)) &&
                !IsSeatExitPseudoGraceActive()) {
                return;
            }
        }
        // FPP in spaceship pilot seat (camera state is kFirstPerson,
        // not kFlight): vanilla ship camera handles FPP positioning.
        {
            auto* cam = RE::PlayerCamera::GetSingleton();
            if (cam && cam->IsInFirstPerson()) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player && IsPlayerInShipPilotSeat(player)) return;
            }
        }

        const bool isIronSights = camera->QCameraEquals(RE::CameraState::kIronSights);
        const bool isShip = IsInShipCameraState(camera);
        const bool isVehicle = IsInVehicleCameraState(camera);

        // Free camera states (photo mode, free fly, free walk): pseudo must
        // NOT override these — the engine's free camera needs to position
        // independently. Without this guard, photo-mode camera ends up far
        // from the player because pseudo forces it to the head bone every
        // frame.
        if (IsInFreeCameraState(camera)) {
            return;
        }

        if (!camera->IsInThirdPerson()) {
            if (!isIronSights && !isShip && !isVehicle) {
                 auto* player = RE::PlayerCharacter::GetSingleton();
                bool safPlaying = player && SAFIntegration::IsSAFAnimationPlaying(player);
                 const bool isFurniture = camera->QCameraEquals(RE::CameraState::kFurniture);
                  bool playerUsingFurniture = player ? IsPlayerUsingFurniture(player) : false;
                  if (isFurniture || playerUsingFurniture) {
                      auto* niCam = FindNiCamera(a_this);
                      if (niCam) {
                          g_NiCamera = niCam;
                          g_CameraRoot = a_this->cameraRoot.get();
                      }
                      InitEyeHeight();
                      HideHead(true);
                      ApplyPseudoFPPRig(a_this, nullptr);
                      ForceCameraToHead();
                      return;
                  }
                  if (!safPlaying) {
                      if (!camera->IsInFirstPerson()) {
                          auto* niCam = FindNiCamera(a_this);
                         if (niCam) {
                             g_NiCamera = niCam;
                             g_CameraRoot = a_this->cameraRoot.get();
                             InitEyeHeight();
                             HideHead(true);
                             ApplyPseudoFPPRig(a_this, nullptr);
                             ForceCameraToHead();
                         }
                     }
                   } else {
                       {
                           auto* niCam = FindNiCamera(a_this);
                           if (niCam) {
                               g_NiCamera = niCam;
                               g_CameraRoot = a_this->cameraRoot.get();
                           }
                           InitEyeHeight();
                           HideHead(true);
                           ApplyPseudoFPPRig(a_this, nullptr);
                           ForceCameraToHead();
                       }
                   }
                  return;
              }
          }

        if (isShip) {
            auto* p = RE::PlayerCharacter::GetSingleton();
            if (p) {
                float bodyYaw = p->GetAngleZ();
                if (!g_FurnitureYawLocked) {
                    g_FurnitureBaseYaw = bodyYaw;
                    g_FurnitureYawLocked = true;
                }
                p->data.angle.z = g_FurnitureBaseYaw;
            }
            return;
        }
        // Iron sights (ADS): let the vanilla iron sights camera position
        // the view at the weapon's sights rather than fighting it with
        // the pseudo head-anchor every frame, for a smoother transition.
        if (isIronSights) {
            return;
        }
         InitEyeHeight();
        if (!g_HasEyeHeight) return;

        {
            auto* niCam = FindNiCamera(a_this);
            if (niCam) {
                g_NiCamera = niCam;
                ApplyPseudoFPPRig(a_this, nullptr);
                ForceCameraToHead();
            }
        }
    }

    void DetourTPSUpdate(void* a_this)
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        auto* tesCam = camera ? static_cast<RE::TESCamera*>(camera) : nullptr;

        // BUGFIX: this hook (ThirdPersonState::Update) is installed and
        // ENABLED unconditionally in Hooks::Setup() and is NEVER disabled
        // by SetNiNodeHooksActive - it runs every single frame the player
        // is in third person, ironsights, ship, or vehicle, regardless of
        // whether pseudo camera has ever been toggled on. It used to call
        // FindNiCamera() - a recursive scene-tree walk - unconditionally
        // on every one of those calls, which is wasted CPU on every frame
        // of ordinary third-person play with pseudo completely off. Only
        // do the walk when pseudo is actually active and would use the
        // result.
        if (g_PseudoFPPActive && tesCam && camera &&
            (camera->IsInThirdPerson() || camera->QCameraEquals(RE::CameraState::kIronSights) || IsInVehicleCameraState(camera))) {
            auto* niCam = FindNiCamera(tesCam);
            if (niCam) {
                g_NiCamera = niCam;
                g_CameraRoot = tesCam->cameraRoot.get();
            }
        }

        if (a_this && g_PseudoFPPActive) {
            float* ptr = reinterpret_cast<float*>((uintptr_t)a_this + 0x1A8);
            ptr[0] = 0.0f;
            ptr[1] = 0.0f;
        }

        origTPSUpdate(a_this);

        // During SAF, zero the orbit/zoom accumulator AFTER the original update
        // as well, because the game may have modified it during origTPSUpdate
        // (e.g. from WASD input). This prevents the player from moving the
        // camera while SAF animation is playing.
        if (a_this && g_PseudoFPPActive && g_SAFAnimationPlaying) {
            float* ptr = reinterpret_cast<float*>((uintptr_t)a_this + 0x1A8);
            ptr[0] = 0.0f;
            ptr[1] = 0.0f;
        }

        if (!camera || (!camera->IsInThirdPerson() && !camera->QCameraEquals(RE::CameraState::kIronSights) && !IsInShipCameraState(camera) && !IsInVehicleCameraState(camera))) return;

        if (g_PseudoFPPActive) {
        }

        InitEyeHeight();
        if (!g_HasEyeHeight) return;

        if (!g_PseudoFPPActive) {
            // BUGFIX (requested guarantee: pseudo-off must be a true no-op
            // for the vanilla game): this used to call HideHead(false) +
            // RestoreCameraOrbit() here, EVERY FRAME, whenever the camera
            // was in third-person/ironsights/ship/vehicle - completely
            // unconditional on whether pseudo had EVER been toggled on
            // this session. RestoreCameraOrbit() forcibly zeroes
            // niCam->local.translate and re-derives cr->local.translate
            // from cr->world.translate every single frame, fighting the
            // engine's own orbit/spring-arm computation continuously in
            // normal third-person play, ironsights, and (very likely,
            // since ThirdPersonState::Update also drives the dialogue
            // camera) NPC dialogue - matching both the "NPC mouths don't
            // move outside pseudo" and "camera briefly swoops on entering
            // dialogue" reports. Explicit one-shot cleanup already happens
            // via DisablePseudoFPPAndRestoreCamera() at the moment pseudo
            // is actually toggled off, so this per-frame call was not just
            // unnecessary but actively harmful the rest of the time.
            g_PrevRootLocal = {};
            g_PrevRootWorld = {};
            g_PrevSetLocal = {};
            g_PrevSetWorld = {};
            g_PrevSetWorldFrame = 0;
            return;
        }

         if (camera && IsInShipCameraState(camera) && !IsSeatExitPseudoGraceActive()) {
             auto* player = RE::PlayerCharacter::GetSingleton();
             if (player) {
                 float bodyYaw = player->GetAngleZ();
                 if (!g_FurnitureYawLocked) {
                     g_FurnitureBaseYaw = bodyYaw;
                     g_FurnitureYawLocked = true;
                 }
                 player->data.angle.z = g_FurnitureBaseYaw;
             }
         } else {
             HideHead(true);
             ApplyPseudoFPPRig(tesCam, a_this);
             ForceCameraToHead();
         }
    }

    void* DetourUpdateWorldData(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateWorldDataFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateWorldDataFunc)g_origUpdateWorldData)(a_this, a_data);
        if (g_PseudoFPPActive && (a_this == g_NiCamera || a_this == g_CameraRoot)) {
            RestorePseudoRig(a_this);
        }
        return result;
    }

    void* DetourUpdateTransformAndBounds(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateTransformAndBoundsFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateTransformAndBoundsFunc)g_origUpdateTransformAndBounds)(a_this, a_data);
        if (g_PseudoFPPActive && (a_this == g_NiCamera || a_this == g_CameraRoot)) {
            RestorePseudoRig(a_this);
        }
        return result;
    }

    void* DetourUpdateTransforms(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateTransformsFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateTransformsFunc)g_origUpdateTransforms)(a_this, a_data);
        if (g_PseudoFPPActive && (a_this == g_NiCamera || a_this == g_CameraRoot)) {
            RestorePseudoRig(a_this);
        }
        return result;
    }

    void ForceCameraToHeadPostSkeleton()
    {
        // During SAF stand-up/sit-down from pilot seat, allow pseudo
        // even in FPP-in-ship context so stand-up animation stays in
        // pseudo camera. IsPlayerInShipPilotSeat is NOT checked — the
        // player leaves the seat as soon as stand-up starts.
        if (g_PseudoFPPInShipActive && !IsShipTransitionPseudoAllowed()) {
            if (!g_SAFAnimationPlaying) return;
        }
        // Normal SAF: ForceCameraToHeadPostSkeleton tracks the head
        // bone during the stand-up motion. No pilot-seat check needed —
        // the player leaves the seat before g_SAFAnimationPlaying drops.
        if (!g_PseudoFPPActive) return;
        // Free camera states: let the engine position the camera independently.
        {
            auto* cam = RE::PlayerCamera::GetSingleton();
            if (cam && IsInFreeCameraState(cam)) return;
        }
        // Ship flight (and FPP-in-ship grace period): do not override.
        {
            auto* cam = RE::PlayerCamera::GetSingleton();
            if (cam && IsInShipCameraState(cam) && !IsSeatExitPseudoGraceActive()) {
                g_FramesSinceShipCameraState = 0;
                return;
            }
            if (!IsShipTransitionPseudoAllowed() && cam && g_FramesSinceShipCameraState <= 360) {
                return;
            }
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto guard = player->loadedData.LockRead();
        auto* loaded = *guard;
        if (!loaded || !loaded->data3D.get()) return;
        g_LastForceFrame = -1;
        ForceCameraToHead();
    }

    void* DetourNiNodeUWD(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateWorldDataFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateWorldDataFunc)g_origNiNodeUWD)(a_this, a_data);
        // Only call RestorePseudoRig when a_this is the camera root or
        // NiCamera itself. The NiNode hooks fire for ALL NiNode instances
        // in the scene (shared vtable) - calling RestorePseudoRig for
        // every skeleton bone / furniture / prop NiNode (thousands per
        // frame) costs ~20 FPS even with its fast-path early returns.
        if (g_PseudoFPPActive && (a_this == g_NiCamera || a_this == g_CameraRoot)) {
            RestorePseudoRig(a_this);
        }
        if (a_this == g_PrevSkeletonRoot) ForceCameraToHeadPostSkeleton();
        return result;
    }

    void* DetourNiNodeUTB(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateTransformAndBoundsFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateTransformAndBoundsFunc)g_origNiNodeUTB)(a_this, a_data);
        if (g_PseudoFPPActive && (a_this == g_NiCamera || a_this == g_CameraRoot)) {
            RestorePseudoRig(a_this);
        }
        if (a_this == g_PrevSkeletonRoot) ForceCameraToHeadPostSkeleton();
        return result;
    }

    void* DetourNiNodeUT(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateTransformsFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateTransformsFunc)g_origNiNodeUT)(a_this, a_data);
        if (g_PseudoFPPActive && (a_this == g_NiCamera || a_this == g_CameraRoot)) {
            RestorePseudoRig(a_this);
        }
        if (a_this == g_PrevSkeletonRoot) ForceCameraToHeadPostSkeleton();
        return result;
    }

    void DetourFPSUpdate(void* a_this, float a_deltaTime)
    {
        if (g_origFPSUpdate) {
            ((void(*)(void*, float))g_origFPSUpdate)(a_this, a_deltaTime);
        }
        if (g_PseudoFPPActive && g_SAFAnimationPlaying) {
            auto* camera = RE::PlayerCamera::GetSingleton();
            if (camera && !IsInShipCameraState(camera)) {
                if (camera->IsInFirstPerson() || IsInFreeCameraState(camera)) {
                    camera->ForceThirdPerson();
                }
            }
            ForceCameraToHead();
        }
    }

     void DetourSetCameraState(RE::PlayerCamera* a_this, RE::CameraState a_newState)
     {
// FPP toggle during ship flight (kFlight → kFirstPerson):
          // the ship camera system handles FPP cockpit view natively.
          // Flag g_PseudoFPPInShipActive so per-frame hooks skip
          // ForceCameraToHead and ApplyPseudoFPPRig during FPP-in-ship.
          // The pseudo rig stays active for SAF stand-up animations.
          {
               bool wasInShipState = a_this && (
                   a_this->QCameraEquals(RE::CameraState::kFlight) ||
                   a_this->QCameraEquals(RE::CameraState::kShipAction) ||
                   a_this->QCameraEquals(RE::CameraState::kShipTargeting) ||
                   a_this->QCameraEquals(RE::CameraState::kShipCombatOrbit) ||
                   a_this->QCameraEquals(RE::CameraState::kShipFarTravel));
                if (wasInShipState && a_newState == RE::CameraState::kFirstPerson) {
                   LogFormatted("SetCameraState(%u): FPP-in-ship detected, g_PseudoFPPInShipActive = true", (uint32_t)a_newState);
                   g_PseudoFPPInShipActive = true;
                   g_PseudoFPPShipExitGraceFrames = 0;
               }
           }

          // Hard disable pseudo-camera when transitioning to any ship
          // flight state — but NOT when the player is still in the
          // pilot seat. For the pilot seat we want to keep pseudo
          // active so the cockpit FPP view stays consistent and the
          // sit/stand animations are not interrupted by a vanilla
          // camera handover.
          if (g_PseudoFPPActive) {
             bool isShipFlightState = false;
             switch (a_newState) {
             case RE::CameraState::kFlight:
             case RE::CameraState::kShipAction:
             case RE::CameraState::kShipTargeting:
             case RE::CameraState::kShipCombatOrbit:
             case RE::CameraState::kShipFarTravel:
                 isShipFlightState = true;
                 break;
             default:
                 break;
             }
               if (isShipFlightState) {
                   auto* player = RE::PlayerCharacter::GetSingleton();
                   if ((!player || !IsPlayerInShipPilotSeat(player)) && !IsSeatExitPseudoGraceActive()) {
                       LogFormatted("SetCameraState(%u): disabling pseudo-camera for ship flight", (uint32_t)a_newState);
                      Patch::UnequipHideHeadgear();
                      g_PseudoFPPActive = false;
                      SetNiNodeHooksActive(false);
                     // Reset camera root and NiCamera to a clean zero state before
                     // the FlightCameraState begins. The pseudo rig has been writing
                     // the player's head position into these transforms while sitting
                     // in the pilot seat (kFurniture) - if we leave those corrupted
                     // values in place, the FlightCameraState's spring/damper
                     // initialises from the head position (inside the cockpit) and
                     // never converges to the correct behind-the-ship position.
                     //
                     // Setting local.translate = {0,0,0} places both nodes at their
                     // parent's origin. After SetCameraState finishes (parent change
                     // to the ship's scene graph), the camera will be at the ship's
                     // origin, which the FlightCameraState can immediately move to
                     // the correct offset.
                     {
                         auto* tesCam = static_cast<RE::TESCamera*>(a_this);
                         auto* cr = tesCam ? tesCam->cameraRoot.get() : nullptr;
                         auto* niCam = tesCam ? FindNiCamera(tesCam) : nullptr;
                         if (cr) {
                             cr->local.translate = {};
                             cr->previousWorld.translate = {};
                             cr->world.translate = {};
                         }
                         if (niCam) {
                             niCam->local.translate = {};
                             niCam->previousWorld.translate = {};
                             niCam->world.translate = {};
                         }
                     }
ClearPseudoCameraPointers();
                      HideHead(false);
                  }
              }
          }

          if (g_PseudoFPPActive && g_SAFAnimationPlaying) {
            bool block = false;
            switch (a_newState) {
            case RE::CameraState::kFirstPerson:
            case RE::CameraState::kFreeWalk:
            case RE::CameraState::kFreeAdvanced:
            case RE::CameraState::kFreeFly:
            case RE::CameraState::kFreeTethered:
            case RE::CameraState::kPhotoMode:
                block = true;
                break;
            // kDialogue: intentionally NOT blocked here anymore (see
            // EventsStarfield.cpp per-frame recovery for
            // RE::CameraState::kDialogue, mirroring the existing Free*
            // recovery pattern). Blocking the call outright prevented the
            // engine's own SetCameraState(kDialogue) logic from ever
            // running at all - including whatever camera-state-tied
            // imagespace/LUT and HUD effects are normally applied on that
            // transition. Letting it through and then recovering back to
            // third person is more work per dialogue start, so if this
            // reintroduces a visible hitch/pop it should be revisited.
            default:
                break;
            }
            if (block) {
                LogFormatted("BLOCKED SetCameraState(%u) during SAF", (uint32_t)a_newState);
                return;
            }
        }
        // Clear FPP-in-ship flag when camera leaves ship context —
        // re-enable pseudo for walking or non-ship states.
        // BUT when exiting the cockpit, keep a short grace window so the
        // stand-up animation can pick pseudo back up before the SAF flag
        // becomes visible to us.
        if (g_PseudoFPPInShipActive && !g_SAFAnimationPlaying && a_newState != RE::CameraState::kFirstPerson) {
            bool newIsShip = (a_newState == RE::CameraState::kFlight ||
                              a_newState == RE::CameraState::kShipAction ||
                              a_newState == RE::CameraState::kShipTargeting ||
                              a_newState == RE::CameraState::kShipCombatOrbit ||
                              a_newState == RE::CameraState::kShipFarTravel);
            if (!newIsShip) {
                g_PseudoFPPInShipActive = false;
                g_PseudoFPPShipExitGraceFrames = kShipExitPseudoGraceFrames;
                LogFormatted("SetCameraState(%u): ship-exit pseudo grace started (%d frames)",
                    (uint32_t)a_newState, kShipExitPseudoGraceFrames);
            }
        }
        if (g_origSetCameraState) {
            ((void(*)(RE::PlayerCamera*, RE::CameraState))g_origSetCameraState)(a_this, a_newState);
        }
    }
}
