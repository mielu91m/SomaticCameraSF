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

#include "starfield/PseudoFPState.h"
#include "starfield/PseudoFP.h"
#include "starfield/SAFIntegration.h"
#include "systems/PseudoFPConfig.h"
#include "RE/B/BGSKeyword.h"
#include "RE/B/BSFixedString.h"
#include "RE/A/AIProcess.h"
#include "RE/T/TESObjectREFR.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <vector>
#include <Windows.h>
#include "RE/B/BSInputEnableManager.h"
#include "RE/B/BSInputEnableLayer.h"
#include "RE/U/UserEvents.h"

namespace Patch {

    // === Global variable definitions ===
    bool g_PseudoFPPActive = false;
    bool g_SAFAnimationPlaying = false;

    RE::NiAVObject* g_HeadBone = nullptr;
    RE::NiAVObject* g_HeadAnchorNode = nullptr;
    RE::NiAVObject* g_HeadMesh = nullptr;
    RE::NiAVObject* g_PrevSkeletonRoot = nullptr;
    RE::NiAVObject* g_HideHeadNode = nullptr;
    bool g_HideHeadNodeValid = false;
    RE::NiPoint3 g_HeadAnchorLocalOffset = {};
    RE::NiPoint3 g_LastValidHeadAnchorWorld = {};
    RE::NiPoint3 g_SAFRigidSmoothedHeadPos = {};
    bool g_HasSAFRigidSmoothedHeadPos = false;

    // A light, unconditional low-pass filter on the raw head-bone reading
    // used only for camera placement during SAF. This is deliberately NOT
    // the old sanity-gated smoothing that caused the camera to freeze on
    // pose changes - there is no rejection logic here at all, it just
    // takes the edge off frame-to-frame jitter (the animation itself can
    // be quite fast/bouncy) so the camera doesn't visibly fight/judder
    // while still tracking real pose changes within a few frames (~50-80ms).
    RE::NiPoint3 g_SAFLastRealFrameHeadPos = {};
    bool g_HasSAFLastRealFrameHeadPos = false;
    std::chrono::steady_clock::time_point g_SAFLastRealFrameTime{};
    int g_FrozenHeadFrameCount = 0;
    int g_SAFSceneAgeTicks = 0;
    int g_SAFPendingTreeWalkCountdown = 0;
    // How long to wait before doing any tree walk after SAF starts. SAF creates
    // transient helper nodes ("AnimObjectA", "Root") in the hierarchy during
    // scene construction; walking those mid-construction causes an access
    // violation. 30 frames (~500ms) is enough for the scene to settle.
    constexpr int kSAFTreeWalkSafeAgeTicks = 30;

    RE::NiPoint3 g_SAFSmoothedHeadPos = {};
    bool g_HasSAFSmoothedHeadPos = false;

    RE::NiPoint3 GetSAFSmoothedHeadPosition()
    {
        if (!g_HeadBone) return {};

        // During SAF, return the peak-held smoothed position so all callers
        // (ComputeSAFRigidEyeWorldPosition, ComputeUltraRigidCameraWorldPosition)
        // use the stable filtered value instead of raw bone reads that alternate
        // between the animated SAF pose and the engine's overwritten idle pose.
        if (g_SAFAnimationPlaying) {
            if (g_HasSAFSmoothedHeadPos) {
                return g_SAFSmoothedHeadPos;
            }
            return g_HeadBone->world.translate;
        }

        const auto now = std::chrono::steady_clock::now();
        if (g_HasSAFLastRealFrameHeadPos) {
            const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - g_SAFLastRealFrameTime).count();
            if (elapsedUs < 2000) {
                return g_SAFLastRealFrameHeadPos;
            }
        }

        const RE::NiPoint3 raw = g_HeadBone->world.translate;
        g_SAFLastRealFrameHeadPos = raw;
        g_HasSAFLastRealFrameHeadPos = true;
        g_SAFLastRealFrameTime = now;
        g_SAFRigidSmoothedHeadPos = raw;
        g_HasSAFRigidSmoothedHeadPos = true;
        return raw;
    }
    bool g_HasLastValidHeadAnchorWorld = false;
    RE::NiPoint3 g_LastRawUltraRigidAnchor = {};
    bool g_HasLastRawUltraRigidAnchor = false;
    RE::NiPoint3 g_SAFPinnedCameraLocalOffset = {};
    bool g_HasSAFPinnedCameraLocalOffset = false;
    float g_EyeHeight = 1.4f;
    bool g_HasEyeHeight = false;
    bool g_WasHeadAppCulled = false;
    bool g_HasSavedHeadCull = false;
    uint32_t g_HeadAnchorMissCount = 0;
    uint32_t g_NeverFoundHeadStreak = 0;
    bool g_ColdStartPseudoResetPending = false;
    int g_FrameCount = 0;

    RE::BSInputEnableLayer* g_SAFInputLayer = nullptr;

    RE::NiCamera* g_NiCamera = nullptr;
    RE::NiAVObject* g_CameraRoot = nullptr;
    RE::NiPoint3 g_PrevRootLocal = {};
    RE::NiPoint3 g_PrevRootWorld = {};
    RE::NiMatrix3 g_PrevRootLocalRot = {};
    RE::NiMatrix3 g_PrevRootWorldRot = {};
    RE::NiMatrix3 g_PrevRootPrevWorldRot = {};
    bool g_RestoreRootRotation = false;
    bool g_FurnitureYawLocked = false;
    float g_FurnitureBaseYaw = 0.0f;
    float g_FurnitureSavedBodyYaw = 0.0f;
    bool g_FurnitureYawRestored = false;
    RE::NiPoint3 g_PrevSetLocal = {};
    RE::NiPoint3 g_PrevSetWorld = {};
    int g_PrevSetWorldFrame = 0;
    int g_FurnitureGraceFrames = 0;
    float g_AngleBlendZ = 0.0f;

    bool g_PosTransitionActive = false;
    RE::NiPoint3 g_PosTransitionStartWorld = {};
    std::chrono::steady_clock::time_point g_PosTransitionStartTime{};

    // Raw SAFAPI_IsPlayingAnimation() result for the CURRENT frame, cached
    // by the main per-frame task (EventsStarfield.cpp) so the rest of the
    // pseudo-camera code (ComputePseudoFPPWorldPosition, called from both
    // ApplyPseudoFPPRig and ForceCameraToHead) doesn't each make their own
    // redundant cross-DLL call into StarfieldAnimationFramework.dll every
    // single frame. That function's cost is opaque to us (it's someone
    // else's plugin), so calling it 2-3x per frame instead of once is pure
    // avoidable overhead - this makes it a plain, free field read instead.
    bool g_SAFPlayingRawCached = false;

    // === Logging ===
    static void VLog(const char* fmt, va_list args)
    {
        static std::ofstream log = [] {
            char* userProfile = nullptr;
            size_t len = 0;
            _dupenv_s(&userProfile, &len, "USERPROFILE");
            std::string path = std::string(userProfile ? userProfile : "") + "\\Documents\\My Games\\Starfield\\SFSE\\SomaticCameraSF_debug.log";
            free(userProfile);
            return std::ofstream(path, std::ios::app);
        }();

        if (log) {
            time_t t = time(nullptr);
            struct tm tm;
            localtime_s(&tm, &t);
            char buf[64];
            strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
            log << "[" << buf << "] ";
            char msg[512];
            vsprintf_s(msg, fmt, args);
            log << msg << "\n";

            // BUGFIX: see the matching fix in EventsStarfield.cpp's VLog -
            // batching the flush every 32 calls silently lost the entire
            // log for any short repro session (load in, do one thing,
            // exit), which is exactly the kind of session used to debug
            // this class of report. Flush every call instead.
            log.flush();
        }
    }

    void LogFormatted(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        VLog(fmt, args);
        va_end(args);
    }

    // === Math helpers ===
    RE::NiPoint3 TransformLocalToWorld(const RE::NiMatrix3& rotation, const RE::NiPoint3& localOffset)
    {
        return {
            rotation.entry[0][0] * localOffset.x + rotation.entry[0][1] * localOffset.y + rotation.entry[0][2] * localOffset.z,
            rotation.entry[1][0] * localOffset.x + rotation.entry[1][1] * localOffset.y + rotation.entry[1][2] * localOffset.z,
            rotation.entry[2][0] * localOffset.x + rotation.entry[2][1] * localOffset.y + rotation.entry[2][2] * localOffset.z
        };
    }

    RE::NiPoint3 TransformWorldToLocal(const RE::NiMatrix3& rotation, const RE::NiPoint3& worldOffset)
    {
        RE::NiMatrix3 invRot = rotation.Transpose();
        return {
            invRot.entry[0][0] * worldOffset.x + invRot.entry[0][1] * worldOffset.y + invRot.entry[0][2] * worldOffset.z,
            invRot.entry[1][0] * worldOffset.x + invRot.entry[1][1] * worldOffset.y + invRot.entry[1][2] * worldOffset.z,
            invRot.entry[2][0] * worldOffset.x + invRot.entry[2][1] * worldOffset.y + invRot.entry[2][2] * worldOffset.z
        };
    }

    RE::NiPoint3 ComputeNodeLocalFromWorld(RE::NiAVObject* node, const RE::NiPoint3& desiredWorldPos)
    {
        if (!node || !node->parent) {
            return desiredWorldPos;
        }
        RE::NiPoint3 diff = desiredWorldPos - node->parent->world.translate;
        return TransformWorldToLocal(node->parent->world.rotate, diff);
    }

    // Inverse of ComputeNodeLocalFromWorld: given a LOCAL (parent-relative)
    // translate, returns the resulting WORLD translate given the parent's
    // current world transform. world = parentWorld.translate +
    // parentWorld.rotate * local (same composition convention used by
    // ComputeNodeLocalFromWorld/ComputeNodeLocalRotationFromWorld above).
    RE::NiPoint3 ComputeNodeWorldFromLocal(RE::NiAVObject* node, const RE::NiPoint3& localPos)
    {
        if (!node || !node->parent) {
            return localPos;
        }
        const RE::NiMatrix3& rot = node->parent->world.rotate;
        RE::NiPoint3 rotated = {
            rot.entry[0][0] * localPos.x + rot.entry[0][1] * localPos.y + rot.entry[0][2] * localPos.z,
            rot.entry[1][0] * localPos.x + rot.entry[1][1] * localPos.y + rot.entry[1][2] * localPos.z,
            rot.entry[2][0] * localPos.x + rot.entry[2][1] * localPos.y + rot.entry[2][2] * localPos.z
        };
        return node->parent->world.translate + rotated;
    }

    // Rotation counterpart of ComputeNodeLocalFromWorld: given the ROTATION
    // we want this node to have in world space, returns what its local
    // (parent-relative) rotation needs to be to achieve that, accounting
    // for whatever rotation the parent node currently has. World rotations
    // compose as world = parentWorld * local (same convention as
    // TransformLocalToWorld above), so local = parentWorld^T * world.
    RE::NiMatrix3 ComputeNodeLocalRotationFromWorld(RE::NiAVObject* node, const RE::NiMatrix3& desiredWorldRot)
    {
        if (!node || !node->parent) {
            return desiredWorldRot;
        }
        const RE::NiMatrix3 parentInv = node->parent->world.rotate.Transpose();
        RE::NiMatrix3 result{};
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                float sum = 0.0f;
                for (int k = 0; k < 3; k++) {
                    sum += parentInv.entry[r][k] * desiredWorldRot.entry[k][c];
                }
                result.entry[r][c] = sum;
            }
        }
        return result;
    }

    float ClampFloat(float value, float minValue, float maxValue)
    {
        return (std::max)(minValue, (std::min)(value, maxValue));
    }

    // Arms a new position blend starting from wherever the camera actually
    // is right now. Call this ONCE, at the exact instant a hard switch is
    // about to happen (pseudo activating, pseudo deactivating, or a native
    // camera-state change like the "/" vanity toggle) - i.e. right before
    // the code that used to just snap the translate straight to the target.
    void StartPosTransition(const RE::NiPoint3& fromWorldPos)
    {
        if (!IsFinitePoint(fromWorldPos)) return;
        float duration = Systems::PseudoFPConfigManager::Get().GetCameraTransitionSeconds();
        if (duration <= 0.0001f) {
            // Smoothing disabled via INI - keep old instant-cut behaviour.
            g_PosTransitionActive = false;
            return;
        }
        g_PosTransitionStartWorld = fromWorldPos;
        g_PosTransitionStartTime = std::chrono::steady_clock::now();
        g_PosTransitionActive = true;
    }

    // Ease-in/ease-out (smoothstep) blend from the captured start position
    // toward targetWorldPos. Cheap - one time query and a handful of
    // multiplies - so it's safe to call every frame from the existing
    // per-frame camera writers without any measurable cost. Automatically
    // clears itself once the blend window elapses so it becomes a no-op
    // again outside of transitions.
    RE::NiPoint3 ApplyPosTransition(const RE::NiPoint3& targetWorldPos)
    {
        if (!g_PosTransitionActive) return targetWorldPos;
        if (!IsFinitePoint(targetWorldPos)) {
            g_PosTransitionActive = false;
            return targetWorldPos;
        }
        float duration = Systems::PseudoFPConfigManager::Get().GetCameraTransitionSeconds();
        if (duration <= 0.0001f) {
            g_PosTransitionActive = false;
            return targetWorldPos;
        }
        float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_PosTransitionStartTime).count();
        float t = ClampFloat(elapsed / duration, 0.0f, 1.0f);
        if (t >= 1.0f) {
            g_PosTransitionActive = false;
            return targetWorldPos;
        }
        // Smoothstep for a soft ease-in/ease-out instead of a linear blend
        // (linear reads as a constant-speed "slide", smoothstep reads as a
        // natural camera move).
        float s = t * t * (3.0f - 2.0f * t);
        RE::NiPoint3 blended;
        blended.x = g_PosTransitionStartWorld.x + (targetWorldPos.x - g_PosTransitionStartWorld.x) * s;
        blended.y = g_PosTransitionStartWorld.y + (targetWorldPos.y - g_PosTransitionStartWorld.y) * s;
        blended.z = g_PosTransitionStartWorld.z + (targetWorldPos.z - g_PosTransitionStartWorld.z) * s;
        return blended;
    }

    float WrapAnglePi(float angle)
    {
        while (angle > 3.14159265f) angle -= 6.28318531f;
        while (angle < -3.14159265f) angle += 6.28318531f;
        return angle;
    }

    RE::NiMatrix3 MakeYawMatrix(float yaw)
    {
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        return RE::NiMatrix3(c, -s, 0.0f, 0.0f, s, c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    }

    float DistanceSquared(const RE::NiPoint3& a, const RE::NiPoint3& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    bool IsFinitePoint(const RE::NiPoint3& point)
    {
        return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
    }

    bool NormalizePlanar(RE::NiPoint3& vector)
    {
        vector.z = 0.0f;
        const float len = std::sqrt(vector.x * vector.x + vector.y * vector.y);
        if (len <= 0.0001f) {
            return false;
        }
        vector.x /= len;
        vector.y /= len;
        return true;
    }

    bool Normalize3D(RE::NiPoint3& vector)
    {
        const float len = std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
        if (len <= 0.0001f) {
            return false;
        }
        vector.x /= len;
        vector.y /= len;
        vector.z /= len;
        return true;
    }

    void GetYawAxes(float yaw, RE::NiPoint3& outForward, RE::NiPoint3& outRight)
    {
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        outForward = { s, c, 0.0f };
        outRight = { c, -s, 0.0f };
    }

    void GetPlayerWorldAxes(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight)
    {
        RE::NiAVObject* worldOrientationNode = g_PrevSkeletonRoot ? g_PrevSkeletonRoot : g_HeadBone;
        if (worldOrientationNode) {
            outForward = {
                worldOrientationNode->world.rotate.entry[0][1],
                worldOrientationNode->world.rotate.entry[1][1],
                worldOrientationNode->world.rotate.entry[2][1]
            };
            outRight = {
                worldOrientationNode->world.rotate.entry[0][0],
                worldOrientationNode->world.rotate.entry[1][0],
                worldOrientationNode->world.rotate.entry[2][0]
            };
            if (NormalizePlanar(outForward) && NormalizePlanar(outRight)) {
                return;
            }
        }
        const float yaw = player ? player->GetAngleZ() : 0.0f;
        GetYawAxes(yaw, outForward, outRight);
    }

    void GetPseudoFPAxes(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight)
    {
        GetPlayerWorldAxes(player, outForward, outRight);
    }

    void GetNodeAxes(RE::NiAVObject* node, RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp)
    {
        if (node) {
            outForward = { node->world.rotate.entry[0][1], node->world.rotate.entry[1][1], node->world.rotate.entry[2][1] };
            outRight = { node->world.rotate.entry[0][0], node->world.rotate.entry[1][0], node->world.rotate.entry[2][0] };
            outUp = { node->world.rotate.entry[0][2], node->world.rotate.entry[1][2], node->world.rotate.entry[2][2] };
        } else {
            const float yaw = player ? player->GetAngleZ() : 0.0f;
            GetYawAxes(yaw, outForward, outRight);
            outUp = { 0.0f, 0.0f, 1.0f };
        }
        if (!Normalize3D(outForward)) {
            const float yaw = player ? player->GetAngleZ() : 0.0f;
            GetYawAxes(yaw, outForward, outRight);
        }
        if (!Normalize3D(outRight)) {
            outRight = { outForward.y, -outForward.x, 0.0f };
            Normalize3D(outRight);
        }
        if (!Normalize3D(outUp)) {
            outUp = { 0.0f, 0.0f, 1.0f };
        }
    }

    void GetPlayerBodyAxes3D(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp)
    {
        RE::NiAVObject* bodyNode = g_PrevSkeletonRoot ? g_PrevSkeletonRoot : g_HeadBone;
        if (bodyNode) {
            GetNodeAxes(bodyNode, player, outForward, outRight, outUp);
            return;
        }
        const float yaw = player ? player->GetAngleZ() : 0.0f;
        GetYawAxes(yaw, outForward, outRight);
        outUp = { 0.0f, 0.0f, 1.0f };
    }

    RE::NiPoint3 CrossProduct(const RE::NiPoint3& a, const RE::NiPoint3& b)
    {
        return {
            (a.y * b.z) - (a.z * b.y),
            (a.z * b.x) - (a.x * b.z),
            (a.x * b.y) - (a.y * b.x)
        };
    }

    // === Head tracking ===
    RE::NiCamera* FindNiCamera(RE::TESCamera* tesCam)
    {
        if (!tesCam || !tesCam->cameraRoot) return nullptr;
        auto* rootNode = tesCam->cameraRoot.get();
        if (!rootNode) return nullptr;
        auto* rootNiNode = rootNode->GetAsNiNode();
        if (!rootNiNode) return nullptr;
        {
            uint32_t num = std::min(rootNiNode->children.size(), 256u);
            for (uint32_t i = 0; i < num; i++) {
                auto* childPtr = rootNiNode->children[i].get();
                if (!childPtr) continue;
                auto* cam = starfield_cast<RE::NiCamera*>(childPtr);
                if (cam) return cam;
            }
        }
        {
            uint32_t num = std::min(rootNiNode->children.size(), 256u);
            for (uint32_t i = 0; i < num; i++) {
                auto* childPtr = rootNiNode->children[i].get();
                if (!childPtr) continue;
                auto* childNode = childPtr->GetAsNiNode();
                if (!childNode) continue;
                uint32_t num2 = std::min(childNode->children.size(), 256u);
                for (uint32_t j = 0; j < num2; j++) {
                    auto* grandchild = childNode->children[j].get();
                    if (!grandchild) continue;
                    auto* cam = starfield_cast<RE::NiCamera*>(grandchild);
                    if (cam) return cam;
                }
            }
        }
        return nullptr;
    }

    RE::NiAVObject* GetCachedHeadBone()
    {
        return g_HeadAnchorNode ? g_HeadAnchorNode : g_HeadBone;
    }

    RE::NiPoint3 GetNodeCenter(RE::NiAVObject* node)
    {
        if (!node) return {};
        if (node->worldBound.radius > 0.001f) {
            return node->worldBound.center;
        }
        return node->world.translate;
    }

    RE::NiAVObject* FindPreferredEyeNode(RE::NiAVObject* root)
    {
        if (!root) return nullptr;
        static constexpr const char* kNames[] = { "Eye_Target", "faceBone_C_EyesFat" };
        for (auto* name : kNames) {
            auto* node = root->GetObjectByName(RE::BSFixedString(name));
            if (node) return node;
        }
        return nullptr;
    }

    RE::NiPoint3 GetHeadMeshCenter(RE::NiAVObject* headMesh)
    {
        if (!headMesh) return {};
        if (headMesh->worldBound.radius > 0.001f) {
            return headMesh->worldBound.center;
        }
        return headMesh->world.translate;
    }

    RE::NiPoint3 GetCurrentHeadAnchorWorldPosition()
    {
        if (g_HeadAnchorNode) {
            if (g_HeadAnchorNode == g_HeadBone) {
                return g_HeadBone->world.translate;
            }
            if (g_HeadMesh && g_HeadAnchorNode == g_HeadMesh) {
                return GetHeadMeshCenter(g_HeadMesh);
            }
            const RE::NiPoint3 directAnchor = g_HeadAnchorNode->world.translate;
            if (IsFinitePoint(directAnchor)) {
                return directAnchor;
            }
        }
        if (!g_HeadBone) return {};
        const RE::NiPoint3 localAnchor = TransformLocalToWorld(g_HeadBone->world.rotate, g_HeadAnchorLocalOffset);
        return g_HeadBone->world.translate + localAnchor;
    }

    void UpdateHeadAnchorData()
    {
        g_HeadAnchorNode = nullptr;
        g_HeadAnchorLocalOffset = {};
        if (!g_HeadBone) {
            g_HeadMesh = nullptr;
            return;
        }
        g_HeadMesh = g_HeadBone->parent ? static_cast<RE::NiAVObject*>(g_HeadBone->parent) : g_HeadBone;
        auto* preferred = FindPreferredEyeNode(g_PrevSkeletonRoot);
        if (preferred) {
            g_HeadAnchorNode = preferred;
        } else {
            g_HeadAnchorNode = g_HeadBone->parent ? static_cast<RE::NiAVObject*>(g_HeadBone->parent) : g_HeadBone;
        }
        if (!g_HeadAnchorNode) {
            g_HeadAnchorNode = g_HeadBone;
        }
        RE::NiPoint3 targetCenter = preferred ? preferred->world.translate : GetHeadMeshCenter(g_HeadMesh);
        RE::NiPoint3 headToCenter = targetCenter - g_HeadBone->world.translate;
        g_HeadAnchorLocalOffset = TransformWorldToLocal(g_HeadBone->world.rotate, headToCenter);
    }

    RE::NiPoint3 GetSAFHeadLocalOffset()
    {
        if (g_SAFAnimationPlaying) {
            return { 0.0f, 0.10f, 0.18f };
        }
        if (g_HeadAnchorLocalOffset.z > 0.05f && std::fabs(g_HeadAnchorLocalOffset.x) < 0.05f) {
            return g_HeadAnchorLocalOffset;
        }
        return { 0.0f, 0.10f, 0.18f };
    }

    RE::NiPoint3 BuildSAFPinnedCameraLocalOffset()
    {
        const RE::NiPoint3 headLocalOffset = GetSAFHeadLocalOffset();
        RE::NiPoint3 pinnedOffset = headLocalOffset;
        pinnedOffset.x += Systems::PseudoFPConfigManager::Get().GetSideOffset();
        pinnedOffset.y += Systems::PseudoFPConfigManager::Get().GetForwardOffset();
        pinnedOffset.z += Systems::PseudoFPConfigManager::Get().GetUpOffset();
        return pinnedOffset;
    }

    void GetSAFStableHeadFrame(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp)
    {
        RE::NiPoint3 headForward, headRight, headUp;
        GetNodeAxes(g_HeadBone, player, headForward, headRight, headUp);
        const RE::NiPoint3 worldUp = { 0.0f, 0.0f, 1.0f };
        RE::NiPoint3 stableRight = CrossProduct(worldUp, headForward);
        if (!Normalize3D(stableRight)) {
            stableRight = CrossProduct(headUp, headForward);
        }
        if (!Normalize3D(stableRight)) {
            stableRight = headRight;
            Normalize3D(stableRight);
        }
        RE::NiPoint3 stableUp = CrossProduct(headForward, stableRight);
        if (!Normalize3D(stableUp)) {
            stableUp = worldUp;
        }
        outForward = headForward;
        outRight = stableRight;
        outUp = stableUp;
    }

    bool ComputeSAFCameraModeWorldPosition(RE::PlayerCharacter* player, RE::NiPoint3& outAnchor, RE::NiPoint3& outWorldPos)
    {
        if (!g_HeadBone || !IsFinitePoint(g_HeadBone->world.translate)) {
            return false;
        }
        if (!g_HasSAFPinnedCameraLocalOffset) {
            g_SAFPinnedCameraLocalOffset = BuildSAFPinnedCameraLocalOffset();
            g_HasSAFPinnedCameraLocalOffset = true;
        }
        const RE::NiPoint3 headPos = g_HeadBone->world.translate;
        const RE::NiMatrix3& headRot = g_HeadBone->world.rotate;
        const RE::NiPoint3 anchorLocalOffset = GetSAFHeadLocalOffset();
        const RE::NiPoint3 worldAnchorOffset = TransformLocalToWorld(headRot, anchorLocalOffset);
        outAnchor = headPos + worldAnchorOffset;

        const float userForward = Systems::PseudoFPConfigManager::Get().GetForwardOffset();
        const float userSide = Systems::PseudoFPConfigManager::Get().GetSideOffset();
        const float userUp = Systems::PseudoFPConfigManager::Get().GetUpOffset();

        RE::NiPoint3 stableForward, stableRight, stableUp;
        GetSAFStableHeadFrame(player, stableForward, stableRight, stableUp);

        outWorldPos = outAnchor;
        outWorldPos.x += (stableForward.x * userForward) + (stableRight.x * userSide) + (stableUp.x * userUp);
        outWorldPos.y += (stableForward.y * userForward) + (stableRight.y * userSide) + (stableUp.y * userUp);
        outWorldPos.z += (stableForward.z * userForward) + (stableRight.z * userSide) + (stableUp.z * userUp);
        return true;
    }

    RE::NiPoint3 ComputeSAFHeadAnchorWorldPosition(RE::PlayerCharacter* player)
    {
        if (!g_HeadBone || !IsFinitePoint(g_HeadBone->world.translate)) {
            return GetCurrentHeadAnchorWorldPosition();
        }
        RE::NiPoint3 anchor = {};
        RE::NiPoint3 worldPos = {};
        if (ComputeSAFCameraModeWorldPosition(player, anchor, worldPos)) {
            return anchor;
        }
        return g_HeadBone->world.translate;
    }

    RE::NiPoint3 ComputeUltraRigidHeadAnchorWorldPosition(RE::PlayerCharacter* player)
    {
        (void)player;
        if (g_HeadBone && IsFinitePoint(g_HeadBone->world.translate)) {
            return g_HeadBone->world.translate;
        }
        if (g_HeadMesh) {
            RE::NiPoint3 meshCenter = GetHeadMeshCenter(g_HeadMesh);
            if (IsFinitePoint(meshCenter)) {
                return meshCenter;
            }
        }
        return {};
    }

    RE::NiPoint3 ComputeSAFRigidEyeWorldPosition(RE::PlayerCharacter* player)
    {
        if (!g_HeadBone || !IsFinitePoint(g_HeadBone->world.translate)) {
            return {};
        }
        const RE::NiPoint3 smoothedHeadPos = GetSAFSmoothedHeadPosition();

        // 1:1 pinned to the head bone during SAF — no automatic offsets from
        // normal pseudo-FP mode (fForwardOffset/fUpOffset/fSideOffset).
        // The relationship between the head bone (neck pivot) and the eyes
        // changes with each pose (standing, prone, supine) and we have no
        // reliable rotation data to transform a single offset correctly for
        // all poses.
        //
        // Tune the camera position per pose by setting fSAFOffsetForward,
        // fSAFOffsetSide, and fSAFOffsetUp in SomaticCameraSF.ini.
        // These are applied in the player-yaw-only frame (forward = player's
        // horizontal forward, up = world up).  For example:
        //   fSAFOffsetForward = 0.12  → 12 cm in the player's facing direction
        //   fSAFOffsetUp      = 0.04  → 4  cm upward
        //   fSAFOffsetForward = -0.10 → 10 cm backward (useful for supine)
        const float yaw = player ? (player->GetAngleZ() * 0.01745329252f) : 0.0f;
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);

        float safForward = 0.0f, safSide = 0.0f, safUp = 0.0f;
        Systems::PseudoFPConfigManager::Get().GetSAFOffsets(safForward, safSide, safUp);

        // Player forward direction at heading θ: (sinθ, cosθ)
        // Player right direction at heading θ:  (cosθ, -sinθ)
        RE::NiPoint3 worldOffset = {
            safForward * sy + safSide * cy,
            safForward * cy - safSide * sy,
            safUp
        };

        return smoothedHeadPos + worldOffset;
    }

    // During SAF the camera orientation is rigidly pinned to the head bone's
    // world rotation. Without this the engine keeps swinging the camera root
    // rotation with mouse-look (orbit), which pushes the eye to the side /
    // behind the player's back even though the position is pinned to the head.
    //
    // The head bone's own world rotation uses column 0 as "forward", while the
    // camera looks down column 1 (right = column 0). A straight copy would land
    // ~90deg off, so derive the player yaw from the head bone and build the
    // camera frame with forward = column 1 the way the rest of the rig expects.
    RE::NiMatrix3 ComputeSAFRigidEyeWorldRotation()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !g_HeadBone) {
            return {};
        }
        // Use the player actor's capsule yaw instead of the head bone's
        // rotation. The head bone's world rotation is overwritten by the
        // engine's animation graph every frame, giving a wrong forward
        // direction (e.g. pointing into the ground when prone), which
        // makes the camera appear "behind" or "in front of" the head even
        // though its position is exactly at the bone.
        const float yaw = player->GetAngleZ() * 0.01745329252f; // deg→rad
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        RE::NiMatrix3 rot{};
        rot.entry[0][0] = c;  rot.entry[0][1] = s;  rot.entry[0][2] = 0.0f;
        rot.entry[1][0] = -s; rot.entry[1][1] = c;  rot.entry[1][2] = 0.0f;
        rot.entry[2][0] = 0.0f; rot.entry[2][1] = 0.0f; rot.entry[2][2] = 1.0f;
        return rot;
    }

    // Anatomical pitch of the head bone: the elevation angle (radians,
    // positive = looking up, negative = looking down) of its local +Y
    // ("forward"/look) axis above or below the horizontal plane. This is
    // exactly asin(forward.z), which is well-defined and stable at ANY
    // pitch - unlike yaw (atan2 of the horizontal x/y components), it
    // never runs into a degenerate near-zero-magnitude case, because it
    // only ever reads a single component of a unit vector. That's why we
    // can safely use it (unlike the old yaw-only "stable frame") to make
    // the camera's up/down look follow the actual pose: standing, prone
    // (pitch near -90 deg, facing the mattress), and supine (pitch near
    // +90 deg, facing the ceiling) all fall naturally out of this one
    // formula with no special-casing per posture.
    float ComputeSAFHeadPitchRadians()
    {
        if (!g_HeadBone) {
            return 0.0f;
        }
        const RE::NiMatrix3& hr = g_HeadBone->world.rotate;
        const RE::NiPoint3 fwd = { hr.entry[0][1], hr.entry[1][1], hr.entry[2][1] };
        const float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
        if (len < 0.0001f) {
            return 0.0f;
        }
        const float z = ClampFloat(fwd.z / len, -1.0f, 1.0f);
        return std::asin(z);
    }

    // Rebuilds a camera rotation matrix that keeps the CURRENT horizontal
    // heading (yaw) taken from currentRot, drops any existing roll, and
    // sets the up/down look (pitch) to targetPitchRad exactly. Used to
    // sync the pseudo camera's pitch to the SAF-animated head's actual
    // pitch while still letting the player freely look left/right.
    RE::NiMatrix3 BuildYawPreservingPitchRotation(const RE::NiMatrix3& currentRot, float targetPitchRad)
    {
        // Current right axis (local X, column 0), flattened to the
        // horizontal plane and re-normalized - this is what carries the
        // camera's current yaw, independent of whatever pitch/roll it had.
        RE::NiPoint3 right = { currentRot.entry[0][0], currentRot.entry[1][0], 0.0f };
        if (!Normalize3D(right)) {
            // Degenerate (camera was looking perfectly along world Z,
            // e.g. straight up/down already) - fall back to the head's
            // own current yaw-only frame so we still produce something.
            const RE::NiMatrix3 headYawRot = ComputeSAFRigidEyeWorldRotation();
            right = { headYawRot.entry[0][0], headYawRot.entry[1][0], 0.0f };
            if (!Normalize3D(right)) {
                right = { 1.0f, 0.0f, 0.0f };
            }
        }
        // Flat (pitch = 0) forward vector, 90 deg from right in the
        // horizontal plane, matching the same convention used everywhere
        // else in this file (forward = (sin yaw, cos yaw, 0) when right =
        // (cos yaw, -sin yaw, 0)).
        const RE::NiPoint3 forwardFlat = { -right.y, right.x, 0.0f };

        const float cp = std::cos(targetPitchRad);
        const float sp = std::sin(targetPitchRad);
        const RE::NiPoint3 forward = {
            forwardFlat.x * cp,
            forwardFlat.y * cp,
            sp
        };
        RE::NiPoint3 up = CrossProduct(right, forward);
        if (!Normalize3D(up)) {
            up = { 0.0f, 0.0f, 1.0f };
        }

        RE::NiMatrix3 rot{};
        rot.entry[0][0] = right.x;   rot.entry[0][1] = forward.x;   rot.entry[0][2] = up.x;
        rot.entry[1][0] = right.y;   rot.entry[1][1] = forward.y;   rot.entry[1][2] = up.y;
        rot.entry[2][0] = right.z;   rot.entry[2][1] = forward.z;   rot.entry[2][2] = up.z;
        return rot;
    }

    void EnableSAFInputLock()
    {
        if (g_SAFInputLayer) {
            return;
        }
        auto* mgr = RE::BSInputEnableManager::GetSingleton();
        if (!mgr) {
            LogFormatted("SAF_INPUT_LOCK: no BSInputEnableManager singleton");
            return;
        }
        if (!mgr->AllocateNewLayer(&g_SAFInputLayer, "SomaticCameraSF_SAF")) {
            LogFormatted("SAF_INPUT_LOCK: AllocateNewLayer failed");
            g_SAFInputLayer = nullptr;
            return;
        }
        using UE = RE::USER_EVENT_FLAG;
        using OE = RE::OTHER_EVENT_FLAG;
        // Root the actor during SAF: disable player-driven locomotion so the
        // camera (pinned to the head bone = actor position) cannot drift away
        // from the animated body the way a controllable "ghost" would.
        g_SAFInputLayer->EnableUserEvent(UE::Movement | UE::Sneaking | UE::Fighting, false);
        g_SAFInputLayer->EnableOtherEvent(OE::Sprinting | OE::Running, false);
        LogFormatted("SAF_INPUT_LOCK: enabled (player movement/sneak/fight input disabled)");
    }

    void DisableSAFInputLock()
    {
        if (!g_SAFInputLayer) {
            return;
        }
        using UE = RE::USER_EVENT_FLAG;
        using OE = RE::OTHER_EVENT_FLAG;
        // IMPORTANT: BSInputEnableLayer::DecRef()'s cleanup path
        // (BSInputEnableManager::LayerFreed) is not implemented in
        // CommonLibSF - dropping the layer via DecRef() alone does NOT
        // restore the flags we disabled. Explicitly re-enable them on our
        // layer first, or the player is left permanently unable to move/
        // sneak/fight/sprint once the SAF scene ends.
        g_SAFInputLayer->EnableUserEvent(UE::Movement | UE::Sneaking | UE::Fighting, true);
        g_SAFInputLayer->EnableOtherEvent(OE::Sprinting | OE::Running, true);
        g_SAFInputLayer->DecRef();
        g_SAFInputLayer = nullptr;
        LogFormatted("SAF_INPUT_LOCK: disabled (input restored)");
    }

    RE::NiPoint3 ComputeUltraRigidCameraWorldPosition(RE::PlayerCharacter* player, const RE::NiPoint3& headAnchor, RE::NiAVObject* cr)
    {
        RE::NiPoint3 forwardAxis = {};
        RE::NiPoint3 rightAxis = {};
        RE::NiPoint3 upAxis = { 0.0f, 0.0f, 1.0f };

        if (g_SAFAnimationPlaying && g_HeadBone) {
            return ComputeSAFRigidEyeWorldPosition(player);
        } else if (cr) {
            const float camYaw = std::atan2(cr->world.rotate.entry[1][0], cr->world.rotate.entry[0][0]);
            GetYawAxes(camYaw, forwardAxis, rightAxis);
        } else {
            GetPlayerWorldAxes(player, forwardAxis, rightAxis);
        }

        RE::NiPoint3 worldPos = headAnchor;
        worldPos.x += (forwardAxis.x * Systems::PseudoFPConfigManager::Get().GetForwardOffset()) + (rightAxis.x * Systems::PseudoFPConfigManager::Get().GetSideOffset()) + (upAxis.x * Systems::PseudoFPConfigManager::Get().GetUpOffset());
        worldPos.y += (forwardAxis.y * Systems::PseudoFPConfigManager::Get().GetForwardOffset()) + (rightAxis.y * Systems::PseudoFPConfigManager::Get().GetSideOffset()) + (upAxis.y * Systems::PseudoFPConfigManager::Get().GetUpOffset());
        worldPos.z += (forwardAxis.z * Systems::PseudoFPConfigManager::Get().GetForwardOffset()) + (rightAxis.z * Systems::PseudoFPConfigManager::Get().GetSideOffset()) + (upAxis.z * Systems::PseudoFPConfigManager::Get().GetUpOffset());
        return worldPos;
    }

    RE::NiPoint3 ClampWorldPosToPlayerCapsule(RE::PlayerCharacter* player, const RE::NiPoint3& desiredWorldPos, const RE::NiPoint3& headAnchor)
    {
        if (!player) return desiredWorldPos;
        const RE::NiPoint3 boundMin = player->GetBoundMin();
        const RE::NiPoint3 boundMax = player->GetBoundMax();
        const float playerX = player->GetPositionX();
        const float playerY = player->GetPositionY();
        const float playerZ = player->GetPositionZ();

        float radiusX = (std::max)(std::fabs(boundMin.x - playerX), std::fabs(boundMax.x - playerX));
        float radiusY = (std::max)(std::fabs(boundMin.y - playerY), std::fabs(boundMax.y - playerY));
        float capsuleRadius = (std::max)(radiusX, radiusY);
        if (capsuleRadius < 0.20f || capsuleRadius > 1.00f) {
            capsuleRadius = 0.35f;
        }

        RE::NiPoint3 clamped = desiredWorldPos;
        float offsetX = desiredWorldPos.x - playerX;
        float offsetY = desiredWorldPos.y - playerY;
        float planarLen = std::sqrt(offsetX * offsetX + offsetY * offsetY);
        if (planarLen > capsuleRadius && planarLen > 0.0001f) {
            float scale = capsuleRadius / planarLen;
            clamped.x = playerX + offsetX * scale;
            clamped.y = playerY + offsetY * scale;
        }

        float minZ = boundMin.z;
        float maxZ = boundMax.z;
        if ((maxZ - minZ) < 0.5f || (maxZ - minZ) > 3.5f) {
            minZ = playerZ + 0.20f;
            maxZ = playerZ + (std::max)(g_EyeHeight + 0.10f, 1.20f);
        }
        maxZ = (std::max)(maxZ, headAnchor.z);
        clamped.z = ClampFloat(clamped.z, minZ, maxZ);
        return clamped;
    }

    bool IsReasonableHeadAnchor(RE::PlayerCharacter* player, const RE::NiPoint3& anchorPos)
    {
        if (!player) return false;
        const float dx = anchorPos.x - player->GetPositionX();
        const float dy = anchorPos.y - player->GetPositionY();
        const float planarDist = std::sqrt(dx * dx + dy * dy);
        if (planarDist > Systems::PseudoFPConfigManager::Get().GetUltraRigidMaxPlanarOffset()) return false;
        const float expectedHeight = g_EyeHeight;
        const float actualHeight = anchorPos.z - player->GetPositionZ();
        const float tolerance = Systems::PseudoFPConfigManager::Get().GetUltraRigidHeightTolerance();
        return actualHeight >= (expectedHeight - tolerance) && actualHeight <= (expectedHeight + tolerance);
    }

    bool IsPlausibleHeadAnchor(RE::PlayerCharacter* player, const RE::NiPoint3& anchorPos)
    {
        if (!player || !IsFinitePoint(anchorPos)) return false;
        const float dx = anchorPos.x - player->GetPositionX();
        const float dy = anchorPos.y - player->GetPositionY();
        const float planarDist = std::sqrt(dx * dx + dy * dy);
        if (planarDist > 3.50f) return false;
        const float actualHeight = anchorPos.z - player->GetPositionZ();
        return actualHeight >= 0.20f && actualHeight <= 3.50f;
    }

    static bool IsExactNodeName(RE::NiAVObject* node, const char* expected)
    {
        if (!node || !expected) {
            return false;
        }
        const char* name = node->name.c_str();
        return name && std::strcmp(name, expected) == 0;
    }

    static void AppendHeadCandidate(std::vector<RE::NiAVObject*>& outCandidates, RE::NiAVObject* candidate)
    {
        if (!candidate) {
            return;
        }
        for (auto* existing : outCandidates) {
            if (existing == candidate) {
                return;
            }
        }
        outCandidates.push_back(candidate);
    }

    static void GatherKnownHeadCandidates(RE::NiAVObject* root, std::vector<RE::NiAVObject*>& outCandidates)
    {
        if (!root) {
            return;
        }

        static constexpr const char* kHeadNames[] = {
            "C_Head",
            "NPC Head [Head]",
            "Head",
            "NPC Head"
        };
        for (auto* name : kHeadNames) {
            AppendHeadCandidate(outCandidates, root->GetObjectByName(RE::BSFixedString(name)));
        }
    }

    static float ScoreHeadCandidate(RE::NiAVObject* candidate, RE::PlayerCharacter* player, bool safActive)
    {
        if (!candidate || !player || !IsFinitePoint(candidate->world.translate)) {
            return -1000000.0f;
        }

        float score = 0.0f;
        if (IsExactNodeName(candidate, "C_Head")) {
            score += 300.0f;
        } else if (IsExactNodeName(candidate, "NPC Head [Head]")) {
            score += 220.0f;
        } else if (IsExactNodeName(candidate, "Head")) {
            score += 180.0f;
        } else {
            score += 120.0f;
        }

        if (candidate->GetObjectByName(RE::BSFixedString("Eye_Target")) ||
            candidate->GetObjectByName(RE::BSFixedString("faceBone_C_EyesFat"))) {
            score += 180.0f;
        }

        const bool plausible = safActive ? IsPlausibleHeadAnchor(player, candidate->world.translate) :
                                           IsReasonableHeadAnchor(player, candidate->world.translate);
        score += plausible ? 140.0f : -500.0f;

        if (g_HasLastValidHeadAnchorWorld && IsFinitePoint(g_LastValidHeadAnchorWorld)) {
            const float dist = std::sqrt(DistanceSquared(candidate->world.translate, g_LastValidHeadAnchorWorld));
            score += (std::max)(0.0f, 120.0f - (dist * 80.0f));
        }

        if (g_HeadBone == candidate) {
            // Hysteresis: keep the current SAF head unless another candidate
            // is clearly better, which prevents frame-to-frame flapping.
            score += 90.0f;
        }

        return score;
    }

    static RE::NiAVObject* FindBestHeadCandidate(RE::NiAVObject* root, RE::PlayerCharacter* player, bool safActive)
    {
        if (!root || !player) {
            return nullptr;
        }

        std::vector<RE::NiAVObject*> candidates;
        candidates.reserve(8);
        GatherKnownHeadCandidates(root, candidates);
        if (candidates.empty()) {
            return nullptr;
        }

        RE::NiAVObject* bestCandidate = nullptr;
        float bestScore = -1000000.0f;
        float currentScore = -1000000.0f;
        for (auto* candidate : candidates) {
            const float score = ScoreHeadCandidate(candidate, player, safActive);
            if (candidate == g_HeadBone) {
                currentScore = score;
            }
            if (!bestCandidate || score > bestScore) {
                bestCandidate = candidate;
                bestScore = score;
            }
        }

        if (safActive && g_HeadBone && g_HeadBone->parent && currentScore > -1000000.0f) {
            constexpr float kSwitchMargin = 75.0f;
            if ((currentScore + kSwitchMargin) >= bestScore) {
                return g_HeadBone;
            }
        }

        return bestCandidate;
    }

    void InitEyeHeight()
    {
        if (g_HasEyeHeight) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto guard = player->loadedData.LockRead();
        auto* loaded = *guard;
        if (!loaded || !loaded->data3D.get()) return;
        g_PrevSkeletonRoot = loaded->data3D.get();
        g_HeadBone = FindBestHeadCandidate(g_PrevSkeletonRoot, player, false);
        if (g_HeadBone) {
            UpdateHeadAnchorData();
            RE::NiPoint3 eyeCenter = GetCurrentHeadAnchorWorldPosition();
            if (Systems::PseudoFPConfigManager::Get().IsUltraRigidEnabled()) {
                eyeCenter = ComputeUltraRigidCameraWorldPosition(player, ComputeUltraRigidHeadAnchorWorldPosition(player));
            }
            g_EyeHeight = eyeCenter.z - player->GetPositionZ();
            g_HasEyeHeight = true;
            LogFormatted("Init: C_Head eyeHeight=%.2f", g_EyeHeight);
        }
    }

    void RefreshHeadBone()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto guard = player->loadedData.LockRead();
        auto* loaded = *guard;
        if (!loaded || !loaded->data3D.get()) {
            g_NeverFoundHeadStreak++;
            return;
        }
        if (g_NeverFoundHeadStreak >= 30) {
            LogFormatted("Cold start detected (data3D was missing for %u refresh calls) - pseudo will require a fresh keypress", g_NeverFoundHeadStreak);
            g_ColdStartPseudoResetPending = true;
        }
        g_NeverFoundHeadStreak = 0;
        auto* root = loaded->data3D.get();
        if (root != g_PrevSkeletonRoot) {
            LogFormatted("Skeleton root change: %p -> %p", (void*)g_PrevSkeletonRoot, (void*)root);
            if (g_HeadBone && IsFinitePoint(g_HeadBone->world.translate)) {
                g_LastValidHeadAnchorWorld = g_HeadBone->world.translate;
                g_HasLastValidHeadAnchorWorld = true;
            }
            g_PrevSkeletonRoot = root;
            g_HeadBone = nullptr;
            g_HeadAnchorNode = nullptr;
            g_HeadMesh = nullptr;
            g_HeadAnchorLocalOffset = {};
            g_NiCamera = nullptr;
            g_CameraRoot = nullptr;
            g_PseudoFPPActive = false;
            SetNiNodeHooksActive(false);
        }
        const bool safActive = g_SAFAnimationPlaying || g_SAFPlayingRawCached;
        auto* newBone = FindBestHeadCandidate(g_PrevSkeletonRoot, player, safActive);
        if (newBone) {
            if (safActive && g_HeadBone && g_HeadBone != newBone) {
                LogFormatted("SAF head candidate switch: %p -> %p", (void*)g_HeadBone, (void*)newBone);
            }
            g_HeadBone = newBone;
            UpdateHeadAnchorData();
            g_HeadAnchorMissCount = 0;
            g_HideHeadNode = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("ICSF_Hide_Head"));
            if (!g_HideHeadNode) {
                g_HideHeadNode = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("HideHead"));
            }
            if (!g_HideHeadNode) {
                g_HideHeadNode = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("Hide_Head"));
            }
            g_HideHeadNodeValid = (g_HideHeadNode != nullptr);
        } else {
            RE::NiAVObject* altHead = nullptr;
            static constexpr const char* kAltHeadNames[] = {
                "NPC Head [Head]", "Head", "NPC Head", "C_Head"
            };
            for (auto* name : kAltHeadNames) {
                altHead = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString(name));
                if (altHead) break;
            }
            if (altHead) {
                g_HeadBone = altHead;
                UpdateHeadAnchorData();
                g_HeadAnchorMissCount = 0;
            } else {
                LogFormatted("C_HEAD NOT FOUND in new skeleton, using cached anchor grace=%u", g_HeadAnchorMissCount);
                g_HeadBone = nullptr;
                g_HeadAnchorNode = nullptr;
                g_HeadMesh = nullptr;
                g_HeadAnchorLocalOffset = {};
                g_HeadAnchorMissCount++;
            }
        }
    }

    void HideHead(bool a_hide)
    {
        // NOTE: this used to set/clear the NiAVObject app-culled flag
        // (flags |= 1 / &= ~1) on g_HeadMesh, which is C_Head's PARENT
        // node - i.e. an ancestor of C_Head itself and of the
        // Eye_Target/faceBone anchor nodes the pseudo camera reads every
        // single frame via GetCurrentHeadAnchorWorldPosition() /
        // ComputeSAFCameraModeWorldPosition().
        //
        // Culling a node is a rendering hint, but the engine also uses it
        // to skip updating world transforms for everything under that
        // node (no point computing a transform nothing will draw). Once
        // culled, g_HeadBone->world.translate stops changing - it's
        // frozen at whatever it was the instant HideHead(true) fired -
        // while the animation keeps running on the (still-updating, still
        // visible) rest of the skeleton. Because this was called from
        // RestorePseudoRig(), which fires many times per frame from
        // multiple NiNode hook points, the head subtree spent most of a
        // SAF animation frozen. The pseudo camera, anchored to that
        // frozen head-bone read, appeared to "slide backward away from
        // the model" as the (correctly animating) body moved out from
        // under it, and could then be nudged around freely - a stuck
        // camera, not a moving one.
        //
        // The head is already properly hidden via the ICSF_Hide_Head
        // armor (see EquipHideHeadgear/UnequipHideHeadgear below), which
        // swaps biped visibility through the normal equip system and does
        // NOT touch this node's cull flag or its transform propagation.
        // That path is sufficient on its own, so the manual cull here is
        // now a deliberate no-op - kept only so existing call sites don't
        // need to change.
        (void)a_hide;
    }

    void RestoreCameraOrbit()
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        if (!camera) return;
        auto* tesCam = static_cast<RE::TESCamera*>(camera);
        auto* cr = tesCam->cameraRoot.get();
        if (!cr) return;
        cr->local.translate = ComputeNodeLocalFromWorld(cr, cr->world.translate);
        cr->previousWorld.translate = cr->world.translate;
        auto* niCam = FindNiCamera(tesCam);
        if (niCam) {
            niCam->local.translate = { 0, 0, 0 };
            niCam->previousWorld.translate = cr->world.translate;
            niCam->world.translate = cr->world.translate;
        }
    }

    void ResetPseudoFPPState(bool a_restoreOrbit)
    {
        if (a_restoreOrbit) {
            RestoreCameraOrbit();
            HideHead(false);
        }
        g_CameraRoot = nullptr;
        g_NiCamera = nullptr;
        g_PrevRootLocal = {};
        g_PrevRootWorld = {};
        g_PrevRootLocalRot = {};
        g_PrevRootWorldRot = {};
        g_PrevRootPrevWorldRot = {};
        g_RestoreRootRotation = false;
        g_FurnitureYawLocked = false;
        g_FurnitureBaseYaw = 0.0f;
        g_FurnitureSavedBodyYaw = 0.0f;
        g_FurnitureYawRestored = false;
        g_FurnitureGraceFrames = 0;
        DisableSAFInputLock();
        g_PrevSetLocal = {};
        g_PrevSetWorld = {};
        g_PrevSetWorldFrame = 0;
        g_HeadAnchorMissCount = 0;
        g_HasLastValidHeadAnchorWorld = false;
        g_LastValidHeadAnchorWorld = {};
        g_HasSAFPinnedCameraLocalOffset = false;
        g_SAFPinnedCameraLocalOffset = {};
        g_HasLastRawUltraRigidAnchor = false;
        g_LastRawUltraRigidAnchor = {};
    }

    void ResetCameraNodesToPlayer()
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!camera || !player) return;
        auto* tesCam = static_cast<RE::TESCamera*>(camera);
        auto* cr = tesCam->cameraRoot.get();
        if (!cr) return;
        RE::NiPoint3 playerPos = { player->GetPositionX(), player->GetPositionY(), player->GetPositionZ() };
        cr->local.translate = ComputeNodeLocalFromWorld(cr, playerPos);
        cr->previousWorld.translate = playerPos;
        cr->world.translate = playerPos;
        auto* niCam = FindNiCamera(tesCam);
        if (niCam) {
            niCam->local.translate = {};
            niCam->previousWorld.translate = playerPos;
            niCam->world.translate = playerPos;
        }
        HideHead(false);
    }

    void FullyResetPseudoState()
    {
        // Requested guarantee: after deactivating pseudo via the toggle
        // key, absolutely nothing it discovered/cached should survive -
        // the next activation re-discovers everything from scratch, and
        // in between, nothing pseudo-related is left holding a reference
        // to any actor's skeleton/bone. ResetPseudoFPPState() already
        // clears the camera-transition-related fields; this clears the
        // rest (head/skeleton identity caches, eye height, streak
        // counters) that it deliberately leaves alone since those are
        // meant to persist across brief pseudo on/off flicker (e.g. the
        // ADS in/out path) rather than a full user-driven deactivation.
        g_HeadBone = nullptr;
        g_HeadAnchorNode = nullptr;
        g_HeadMesh = nullptr;
        g_PrevSkeletonRoot = nullptr;
        g_HideHeadNode = nullptr;
        g_HideHeadNodeValid = false;
        g_HeadAnchorLocalOffset = {};
        g_LastValidHeadAnchorWorld = {};
        g_HasLastValidHeadAnchorWorld = false;
        g_SAFRigidSmoothedHeadPos = {};
        g_HasSAFRigidSmoothedHeadPos = false;
        g_SAFLastRealFrameHeadPos = {};
        g_HasSAFLastRealFrameHeadPos = false;
        g_SAFSmoothedHeadPos = {};
        g_HasSAFSmoothedHeadPos = false;
        g_HasSAFPinnedCameraLocalOffset = false;
        g_SAFPinnedCameraLocalOffset = {};
        g_HasLastRawUltraRigidAnchor = false;
        g_LastRawUltraRigidAnchor = {};
        g_HasEyeHeight = false;
        g_EyeHeight = 1.4f;
        g_HasSavedHeadCull = false;
        g_HeadAnchorMissCount = 0;
        g_NeverFoundHeadStreak = 0;
        g_CameraRoot = nullptr;
        g_NiCamera = nullptr;
    }

    void DisablePseudoFPPAndRestoreCamera()
    {
        // ResetPseudoFPPState()'s RestoreCameraOrbit() alone is NOT enough
        // for a manual (F4/F8) disable: it bakes in whatever WORLD position
        // the camera currently has - i.e. the pseudo head anchor from the
        // last active frame - as the new orbit base, instead of reverting
        // to the real vanilla camera position. Left uncorrected, the
        // engine's own camera object (the same one Starfield uses for NPC
        // animation/AI LOD distance checks and for build-mode "in range"
        // checks) stays permanently offset at the head position after the
        // toggle key is pressed. That single stale offset is what makes
        // NPCs freeze/slide/become untargetable and build objects show as
        // "out of range" - but ONLY once pseudo has been toggled off,
        // because that's the only time this broken restore path runs.
        //
        // Fix: prefer restoring the exact pre-pseudo transforms captured
        // in ApplyPseudoFPPRig (g_SavedRootLocal/g_SavedNiCamLocal), the
        // same snapshot the automatic disable path (ADS/SAF-end) already
        // uses correctly. If no snapshot exists yet (e.g. pseudo was
        // toggled off in the same frame it was toggled on), fall back to
        // snapping the camera straight to the player's actual position
        // instead of leaving it wherever pseudo last wrote it.
        auto* camera = RE::PlayerCamera::GetSingleton();
        bool restored = false;
        if (Patch::g_SavedTransformsValid && camera) {
            auto* tesCam = static_cast<RE::TESCamera*>(camera);
            auto* cr = tesCam ? tesCam->cameraRoot.get() : nullptr;
            auto* niCam = tesCam ? FindNiCamera(tesCam) : nullptr;
            if (cr) {
                cr->local.translate = Patch::g_SavedRootLocal;
                // CRITICAL FIX: also recompute world.translate/
                // previousWorld.translate to match the restored local -
                // otherwise these fields keep holding the pseudo head
                // position for at least one frame after disabling (until
                // the next natural UpdateWorldData pass happens to
                // recompute them). The engine's own NPC LOD/animation/AI
                // distance checks read world.translate directly, so that
                // stale frame is exactly what causes NPCs to freeze,
                // slide, or clip through each other right after toggling
                // pseudo off - the same class of bug already fixed for
                // the ship-flight disable path in ClearPseudoCameraPointers.
                RE::NiPoint3 crWorld = ComputeNodeWorldFromLocal(cr, cr->local.translate);
                cr->world.translate = crWorld;
                cr->previousWorld.translate = crWorld;
            }
            if (niCam) {
                niCam->local.translate = Patch::g_SavedNiCamLocal;
                // niCam->parent is cr (already updated above), so this
                // picks up the freshly-restored cr->world transform.
                RE::NiPoint3 niCamWorld = ComputeNodeWorldFromLocal(niCam, niCam->local.translate);
                niCam->world.translate = niCamWorld;
                niCam->previousWorld.translate = niCamWorld;
            }
            Patch::g_SavedTransformsValid = false;
            restored = (cr != nullptr);
        }
        // Don't let ResetPseudoFPPState's RestoreCameraOrbit() re-bake the
        // (now stale/pseudo) world.translate on top of what we just
        // restored above - only run the rest of its cleanup.
        ResetPseudoFPPState(/*a_restoreOrbit=*/false);
        if (!restored) {
            // No usable snapshot - hard fallback so the camera doesn't
            // stay parked at the last pseudo position indefinitely.
            ResetCameraNodesToPlayer();
        }
        FullyResetPseudoState();
    }

    void ClearPseudoCameraPointers()
    {
        // Reset the camera root's local translate to zero so the vanilla
        // ship camera system doesn't inherit our stale pseudo offset.
        // Without this, cameraRoot->local.translate still contains the
        // last world-derived local offset we set, and the ship camera
        // system adds that on top of its own positioning - making the
        // camera appear somewhere in space away from the ship.
        // NOTE: a previous attempt also did this but was reverted because
        // ResetCameraNodesToPlayer() was ALSO being called at the same
        // time, which snapped the camera to the player's ground position
        // and then zeroing local.translate on top of that caused issues.
        // Now that ResetCameraNodesToPlayer() is no longer called for
        // ship flight transitions, zeroing local.translate is safe and
        // necessary for the ship camera to position itself correctly.
        //
        // CRITICAL FIX: we must ALSO reset world.translate and
        // previousWorld.translate. The pseudo rig writes the player's
        // head-world position into these fields every frame while active.
        // If we only zero local.translate but leave world.translate
        // stale, the ship camera system reads the old ground-level
        // position (or worse, a position far from the ship) and the
        // camera spawns outside the ship in space. Resetting world
        // translate to the parent's current world position (which is
        // what a zero local.translate would produce after the next
        // UpdateWorldData) ensures the cached world transform is
        // consistent with the zeroed local transform immediately,
        // without waiting for a transform update pass.
        {
            auto* camera = RE::PlayerCamera::GetSingleton();
            if (camera) {
                auto* tesCam = static_cast<RE::TESCamera*>(camera);
                auto* cr = tesCam->cameraRoot.get();
                if (cr) {
                    cr->local.translate = { 0.0f, 0.0f, 0.0f };
                    // Reset cached world/previousWorld translate to match
                    // the zeroed local translate. When local is zero,
                    // world = parent->world.translate (plus any rotation
                    // offset, but with zero local the translation component
                    // is just the parent's world position).
                    if (cr->parent) {
                        cr->world.translate = cr->parent->world.translate;
                        cr->previousWorld.translate = cr->parent->world.translate;
                    } else {
                        cr->world.translate = { 0.0f, 0.0f, 0.0f };
                        cr->previousWorld.translate = { 0.0f, 0.0f, 0.0f };
                    }
                }
                auto* niCam = FindNiCamera(tesCam);
                if (niCam) {
                    niCam->local.translate = { 0.0f, 0.0f, 0.0f };
                    // niCam's parent is cr (or a child of cr), so with
                    // zeroed local translate its world translate should
                    // match cr's world translate.
                    if (cr) {
                        niCam->world.translate = cr->world.translate;
                        niCam->previousWorld.translate = cr->world.translate;
                    } else {
                        niCam->world.translate = { 0.0f, 0.0f, 0.0f };
                        niCam->previousWorld.translate = { 0.0f, 0.0f, 0.0f };
                    }
                }
            }
        }
        g_NiCamera = nullptr;
        g_CameraRoot = nullptr;
    }

    // === Detection functions ===
    bool IsPlayerUsingFurniture(RE::PlayerCharacter* player)
    {
        if (!player) return false;
        auto* proc = player->currentProcess;
        if (!proc || !proc->middleHigh) return false;
        return static_cast<bool>(proc->middleHigh->occupiedFurniture) || static_cast<bool>(proc->middleHigh->currentFurniture);
    }

    bool IsInVehicleCameraState(RE::PlayerCamera* camera)
    {
        if (!camera) return false;
        return camera->QCameraEquals(RE::CameraState::kVehicle);
    }

    bool IsInFreeCameraState(RE::PlayerCamera* camera)
    {
        if (!camera) return false;
        return camera->QCameraEquals(RE::CameraState::kFreeWalk) ||
               camera->QCameraEquals(RE::CameraState::kFreeAdvanced) ||
               camera->QCameraEquals(RE::CameraState::kFreeFly) ||
               camera->QCameraEquals(RE::CameraState::kFreeTethered) ||
               camera->QCameraEquals(RE::CameraState::kPhotoMode);
    }

    static void GetPseudoFPOffsets(float& outX, float& outY, float& outZ)
    {
        Systems::PseudoFPConfigManager::Get().GetOffsets(outX, outY, outZ);
    }

    bool ComputePseudoFPPWorldPosition(RE::TESCamera* tesCam, RE::NiPoint3& outWorldPos, RE::NiPoint3* outHeadAnchor, bool* outUsingFallback)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !tesCam) {
            return false;
        }

        // After exiting to the main menu and loading a save, the entire
        // scene graph is rebuilt: the player's skeleton AND the camera
        // node tree are recreated, so every cached NiAVObject pointer
        // (g_PrevSkeletonRoot, g_HeadBone, g_HeadAnchorNode, g_HeadMesh,
        // g_CameraRoot, g_NiCamera) points into FREED memory. The first
        // pseudo frame after such a load dereferences those (e.g. the
        // needFreshLookup check below reads g_HeadBone->parent) and
        // crashes with an access violation. Re-validate the skeleton
        // identity BEFORE any cached node is touched: if the player's
        // current data3D root differs from the one we cached, clear every
        // cached node pointer so the code re-discovers everything from
        // scratch below instead of dereferencing freed nodes.
        {
            auto guard = player->loadedData.LockRead();
            auto* loaded = *guard;
            if (loaded && loaded->data3D.get()) {
                auto* curRoot = loaded->data3D.get();
                if (curRoot != g_PrevSkeletonRoot) {
                    LogFormatted("PseudoFP: scene rebuilt (skeleton root changed) %p -> %p - clearing cached node pointers", (void*)g_PrevSkeletonRoot, (void*)curRoot);
                    g_PrevSkeletonRoot = curRoot;
                    g_HeadBone = nullptr;
                    g_HeadAnchorNode = nullptr;
                    g_HeadMesh = nullptr;
                    g_HideHeadNode = nullptr;
                    g_HideHeadNodeValid = false;
                    g_HeadAnchorLocalOffset = {};
                    g_LastValidHeadAnchorWorld = {};
                    g_HasLastValidHeadAnchorWorld = false;
                    g_CameraRoot = nullptr;
                    g_NiCamera = nullptr;
                    g_NeverFoundHeadStreak = 0;
                    g_ColdStartPseudoResetPending = false;
                    // Force InitEyeHeight() to re-run for the new skeleton.
                    g_HasEyeHeight = false;
                }
            }
        }

        InitEyeHeight();

        auto* cr = tesCam->cameraRoot.get();
        if (!cr) {
            return false;
        }

        // Reuse this frame's cached raw SAF query (set once by the main
        // per-frame task) instead of calling into StarfieldAnimationFramework.dll
        // again here - this function can run 1-2 more times per frame
        // (ApplyPseudoFPPRig every frame, ForceCameraToHead when its 2ms
        // same-frame cache misses), and each call used to re-hit the SAF
        // DLL redundantly for a result that can't have changed within the
        // same frame.
        const bool safActive = g_SAFAnimationPlaying || g_SAFPlayingRawCached;
        if (g_SAFAnimationPlaying) {
            g_SAFSceneAgeTicks++;
        }

        // SAF can expose multiple similarly named head nodes (animated copy
        // plus a resting copy). We re-evaluate every call, but the selector
        // above applies hysteresis so the camera stays stable while still
        // being able to switch to a better candidate when the active SAF pose
        // changes and the previously chosen head stops matching the visual body.
        // See long comment above ComputePseudoFPPWorldPosition's SAF
        // branch: re-resolving the head bone every call during an active
        // SAF scene is what causes the alternation between the real,
        // animated skeleton and the engine's separate resting copy. Once
        // locked onto a valid bone at the SAF-start edge, keep using that
        // exact node for the rest of the scene instead of re-querying.
        // HOWEVER, if the head bone position is frozen (not updating), we need
        // to refresh to find the animated copy.
        bool needFreshLookup = !g_SAFAnimationPlaying || !g_HeadBone || !g_HeadBone->parent;
        // One-shot scheduled refresh: after SAF start we wait for the scene
        // to settle, then force a single RefreshHeadBone() so the camera
        // picks up the animated head copy without waiting for the frozen-
        // head detector (~90 frames with both gates).
        if (g_SAFAnimationPlaying && g_SAFPendingTreeWalkCountdown > 0) {
            g_SAFPendingTreeWalkCountdown--;
            if (g_SAFPendingTreeWalkCountdown == 0) {
                needFreshLookup = true;
            }
        }
        const bool sceneOldEnoughForTreeWalk = !g_SAFAnimationPlaying || g_SAFSceneAgeTicks >= kSAFTreeWalkSafeAgeTicks;
        if (!needFreshLookup && g_SAFAnimationPlaying && g_HeadBone && g_HasLastValidHeadAnchorWorld && sceneOldEnoughForTreeWalk) {
            const float dx = g_HeadBone->world.translate.x - g_LastValidHeadAnchorWorld.x;
            const float dy = g_HeadBone->world.translate.y - g_LastValidHeadAnchorWorld.y;
            const float dz = g_HeadBone->world.translate.z - g_LastValidHeadAnchorWorld.z;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            // If head hasn't moved at all for several frames, it's likely frozen
            // on the resting copy - refresh to find the animated copy
            if (dist < 0.001f) {
                g_FrozenHeadFrameCount++;
                if (g_FrozenHeadFrameCount > 30) {
                    needFreshLookup = true;
                    g_FrozenHeadFrameCount = 0;
                }
            } else {
                g_FrozenHeadFrameCount = 0;
            }
        }
        // A fresh SAF scene can still be mid-construction/mid-teardown of
        // its transient helper nodes (observed crashing inside a node-tree
        // walk, with scene-construction node names in the crash registers,
        // right around SAF start). Don't walk the tree ourselves - not even
        // via the frozen-head re-check above - until the scene has had time
        // to settle, no matter which trigger asked for it.
        if (needFreshLookup && g_SAFAnimationPlaying && !sceneOldEnoughForTreeWalk) {
            needFreshLookup = false;
        }
        if (needFreshLookup) {
            RefreshHeadBone();
        }

        if (!safActive && g_HeadBone && g_HeadBone->parent) {
            RE::NiPoint3 eyeWorld = GetCurrentHeadAnchorWorldPosition();
            if (IsFinitePoint(eyeWorld)) {
                RE::NiPoint3 headToEye = eyeWorld - g_HeadBone->world.translate;
                g_HeadAnchorLocalOffset = TransformWorldToLocal(g_HeadBone->world.rotate, headToEye);
            }
        }

        if (!safActive) {
            g_HasSAFPinnedCameraLocalOffset = false;
            g_HasLastRawUltraRigidAnchor = false;
            g_HasSAFSmoothedHeadPos = false;
        }

        bool usingFallback = false;
        RE::NiPoint3 anchorPos;
        const bool ultraRigidHeadAttach = Systems::PseudoFPConfigManager::Get().IsUltraRigidEnabled();
        const RE::NiPoint3 playerEyeFallback = {
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ() + g_EyeHeight
        };
        if (g_HeadBone) {
            if (safActive && IsFinitePoint(g_HeadBone->world.translate)) {
                RE::NiPoint3 rawHeadPos = g_HeadBone->world.translate;

                // Minimal velocity clamp: the engine intermittently overwrites
                // SAF's bone transforms with the idle pose. These single-frame
                // teleports are caught by the clamp and limited to 0.25m.
                // Between overwrites, rawHeadPos reflects the true SAF position
                // — no averaging or smoothing needed (they would pull the camera
                // toward the midpoint of the two poses, behind the eyes).
                {
                    if (g_HasSAFSmoothedHeadPos) {
                        const RE::NiPoint3 delta = rawHeadPos - g_SAFSmoothedHeadPos;
                        const float deltaLen = delta.Length();
                        constexpr float kMaxDelta = 0.25f;
                        if (deltaLen > kMaxDelta) {
                            const float scale = kMaxDelta / deltaLen;
                            g_SAFSmoothedHeadPos = g_SAFSmoothedHeadPos + delta * scale;
                        } else {
                            g_SAFSmoothedHeadPos = rawHeadPos;
                        }
                    } else {
                        g_SAFSmoothedHeadPos = rawHeadPos;
                        g_HasSAFSmoothedHeadPos = true;
                    }
                }

                RE::NiPoint3 worldPos = ComputeSAFRigidEyeWorldPosition(player);
                anchorPos = g_HasSAFSmoothedHeadPos ? g_SAFSmoothedHeadPos : rawHeadPos;
                outWorldPos = worldPos;
                g_LastValidHeadAnchorWorld = anchorPos;
                g_HasLastValidHeadAnchorWorld = true;
                if (outHeadAnchor) *outHeadAnchor = anchorPos;
                if (outUsingFallback) *outUsingFallback = false;
                return true;
            }
            if (ultraRigidHeadAttach) {
                RE::NiPoint3 directBoneAnchor = ComputeUltraRigidHeadAnchorWorldPosition(player);

                const bool ignoreUltraRigidSanity = Systems::PseudoFPConfigManager::Get().IsUltraRigidIgnoreSanityEnabled();
                bool acceptUltraRigidAnchor = IsFinitePoint(directBoneAnchor);

                // A generous plausibility check (same one used by the
                // non-ultra-rigid path further down) catches transient
                // spikes - e.g. the head bone briefly reading a position
                // behind/inside a wall during a furniture get-up
                // animation - without interfering with legitimate large
                // pose changes like standing -> lying during SAF (those
                // stay well inside these generous bounds). Falls back to
                // the last known-good anchor for that one bad frame
                // rather than snapping the camera through geometry.
                if (acceptUltraRigidAnchor && !g_SAFAnimationPlaying && !ignoreUltraRigidSanity &&
                    g_HasLastValidHeadAnchorWorld && !IsPlausibleHeadAnchor(player, directBoneAnchor)) {
                    directBoneAnchor = g_LastValidHeadAnchorWorld;
                }

                if (acceptUltraRigidAnchor) {
                    const RE::NiPoint3 rawThisFrame = directBoneAnchor;

                    if (g_SAFAnimationPlaying && g_HasLastValidHeadAnchorWorld && !ignoreUltraRigidSanity) {
                        constexpr float kSmoothingAlpha = 0.25f;
                        RE::NiPoint3 smoothed;
                        smoothed.x = g_LastValidHeadAnchorWorld.x + (directBoneAnchor.x - g_LastValidHeadAnchorWorld.x) * kSmoothingAlpha;
                        smoothed.y = g_LastValidHeadAnchorWorld.y + (directBoneAnchor.y - g_LastValidHeadAnchorWorld.y) * kSmoothingAlpha;
                        smoothed.z = g_LastValidHeadAnchorWorld.z + (directBoneAnchor.z - g_LastValidHeadAnchorWorld.z) * kSmoothingAlpha;

                        directBoneAnchor = smoothed;
                    }

                    g_LastRawUltraRigidAnchor = rawThisFrame;
                    g_HasLastRawUltraRigidAnchor = true;
                }

                if (acceptUltraRigidAnchor) {
                    anchorPos = directBoneAnchor;
                    g_LastValidHeadAnchorWorld = anchorPos;
                    g_HasLastValidHeadAnchorWorld = true;

                    outWorldPos = ComputeUltraRigidCameraWorldPosition(player, anchorPos, cr);



                    if (outHeadAnchor) {
                        *outHeadAnchor = anchorPos;
                    }
                    if (outUsingFallback) {
                        *outUsingFallback = false;
                    }
                    return true;
                }
            }

            RE::NiPoint3 headAnchorPos;
            if (g_SAFAnimationPlaying) {
                // During SAF, compute the camera position using head-relative
                // axes (via GetSAFStableHeadFrame) instead of body axes.
                // The head bone's orientation changes between poses (standing,
                // prone, on back) and body-relative axes push the camera in
                // the wrong direction. We then return early before the common
                // offset code (which uses body axes).
                if (g_HeadBone && IsFinitePoint(g_HeadBone->world.translate)) {
                    auto guard = player->loadedData.LockRead();
                    auto* loaded = *guard;
                    if (loaded && loaded->data3D.get()) {
                        auto* root = loaded->data3D.get();
                        // Use head bone position clamped to root node
                        RE::NiPoint3 rawHeadPos = g_HeadBone->world.translate;
                        RE::NiPoint3 rootPos = root->world.translate;
                        constexpr float kMaxDrift = 100.0f;
                        float dx = rawHeadPos.x - rootPos.x;
                        float dy = rawHeadPos.y - rootPos.y;
                        float dz = rawHeadPos.z - rootPos.z;
                        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                        if (dist > kMaxDrift) {
                            float scale = kMaxDrift / dist;
                            rawHeadPos.x = rootPos.x + dx * scale;
                            rawHeadPos.y = rootPos.y + dy * scale;
                            rawHeadPos.z = rootPos.z + dz * scale;
                        }
                        // Minimal velocity clamp (same as main SAF branch)
                        {
                            if (g_HasSAFSmoothedHeadPos) {
                                const RE::NiPoint3 delta = rawHeadPos - g_SAFSmoothedHeadPos;
                                const float deltaLen = delta.Length();
                                constexpr float kMaxDelta = 0.25f;
                                if (deltaLen > kMaxDelta) {
                                    const float scale = kMaxDelta / deltaLen;
                                    headAnchorPos = g_SAFSmoothedHeadPos + delta * scale;
                                    g_SAFSmoothedHeadPos = headAnchorPos;
                                } else {
                                    headAnchorPos = rawHeadPos;
                                    g_SAFSmoothedHeadPos = rawHeadPos;
                                }
                            } else {
                                headAnchorPos = rawHeadPos;
                                g_SAFSmoothedHeadPos = rawHeadPos;
                                g_HasSAFSmoothedHeadPos = true;
                            }
                        }
                        // Apply INI offsets using head-relative axes, then return early
                        RE::NiPoint3 fwd, right, up;
                        GetSAFStableHeadFrame(player, fwd, right, up);
                        // Nose forward offset
                        const float noseFwd = Systems::PseudoFPConfigManager::Get().GetNoseForward();
                        headAnchorPos.x += fwd.x * noseFwd;
                        headAnchorPos.y += fwd.y * noseFwd;
                        headAnchorPos.z += fwd.z * noseFwd;
                        // INI offset values
                        float offX, offY, offZ;
                        Systems::PseudoFPConfigManager::Get().GetOffsets(offX, offY, offZ);
                        headAnchorPos.x += (-fwd.x * offX) + (right.x * offY) + (up.x * offZ);
                        headAnchorPos.y += (-fwd.y * offX) + (right.y * offY) + (up.y * offZ);
                        headAnchorPos.z += (-fwd.z * offX) + (right.z * offY) + (up.z * offZ);
                        // Debug offsets
                        float debugX, debugY, debugZ;
                        Systems::PseudoFPConfigManager::Get().GetDebugOffsets(debugX, debugY, debugZ);
                        headAnchorPos.x += debugX;
                        headAnchorPos.y += debugY;
                        headAnchorPos.z += debugZ;
                        anchorPos = headAnchorPos;
                        outWorldPos = anchorPos;
                        g_LastValidHeadAnchorWorld = anchorPos;
                        g_HasLastValidHeadAnchorWorld = true;
                        if (outHeadAnchor) *outHeadAnchor = anchorPos;
                        if (outUsingFallback) *outUsingFallback = false;
                        return true;
                    }
                }
                headAnchorPos = playerEyeFallback;
            } else {
                headAnchorPos = GetCurrentHeadAnchorWorldPosition();
            }

            RE::NiPoint3 directAnchorPos = headAnchorPos;
            const bool ignoreSanity = (ultraRigidHeadAttach && Systems::PseudoFPConfigManager::Get().IsUltraRigidIgnoreSanityEnabled()) || g_SAFAnimationPlaying;
            const bool acceptDirectAnchor = IsFinitePoint(directAnchorPos) &&
                (ignoreSanity || (ultraRigidHeadAttach ? IsPlausibleHeadAnchor(player, directAnchorPos) : IsReasonableHeadAnchor(player, directAnchorPos)));

            if (acceptDirectAnchor) {
                anchorPos = directAnchorPos;
                g_LastValidHeadAnchorWorld = anchorPos;
                g_HasLastValidHeadAnchorWorld = true;
            } else if (g_HasLastValidHeadAnchorWorld &&
                       (ignoreSanity ? IsFinitePoint(g_LastValidHeadAnchorWorld) :
                                       (ultraRigidHeadAttach ? IsPlausibleHeadAnchor(player, g_LastValidHeadAnchorWorld) :
                                                               IsReasonableHeadAnchor(player, g_LastValidHeadAnchorWorld)))) {
                anchorPos = g_LastValidHeadAnchorWorld;
            } else {
                usingFallback = true;
                anchorPos = playerEyeFallback;
                g_HasLastValidHeadAnchorWorld = false;
            }
        } else {
            if (g_SAFAnimationPlaying) {
                usingFallback = true;
                if (g_HasLastValidHeadAnchorWorld && IsFinitePoint(g_LastValidHeadAnchorWorld)) {
                    anchorPos = g_LastValidHeadAnchorWorld;
                } else if (g_PrevSkeletonRoot && IsFinitePoint(g_PrevSkeletonRoot->world.translate)) {
                    anchorPos = g_PrevSkeletonRoot->world.translate;
                    anchorPos.z += g_EyeHeight;
                    RE::NiAVObject* headNode = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("NPC Head [Head]"));
                    if (!headNode) {
                        headNode = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("Head"));
                    }
                    if (headNode && IsFinitePoint(headNode->world.translate)) {
                        anchorPos.z = headNode->world.translate.z;
                    }
                } else {
                    anchorPos = playerEyeFallback;
                }
            } else {
                const uint32_t kHeadAnchorGraceFrames = Systems::PseudoFPConfigManager::Get().GetHeadCacheGraceFrames();
                constexpr float kMaxCachedAnchorDistanceSq = 6.25f;
                const bool canUseCachedAnchor = ultraRigidHeadAttach ?
                    (g_HasLastValidHeadAnchorWorld && g_HeadAnchorMissCount <= kHeadAnchorGraceFrames && IsFinitePoint(g_LastValidHeadAnchorWorld)) :
                    (g_HasLastValidHeadAnchorWorld &&
                     g_HeadAnchorMissCount <= kHeadAnchorGraceFrames &&
                     DistanceSquared(g_LastValidHeadAnchorWorld, playerEyeFallback) <= kMaxCachedAnchorDistanceSq);

                if (canUseCachedAnchor) {
                    anchorPos = g_LastValidHeadAnchorWorld;
                    if (ultraRigidHeadAttach) {
                        outWorldPos = ComputeUltraRigidCameraWorldPosition(player, anchorPos, cr);
                        if (outHeadAnchor) {
                            *outHeadAnchor = anchorPos;
                        }
                        if (outUsingFallback) {
                            *outUsingFallback = false;
                        }
                        return true;
                    }
                } else {
                    usingFallback = true;
                    anchorPos = playerEyeFallback;
                    g_HasLastValidHeadAnchorWorld = false;
                }
            }
        }

        const float noseForward = Systems::PseudoFPConfigManager::Get().GetNoseForward();
        RE::NiPoint3 forwardAxis = {};
        RE::NiPoint3 rightAxis = {};
        RE::NiPoint3 upAxis = { 0.0f, 0.0f, 1.0f };
        GetPseudoFPAxes(player, forwardAxis, rightAxis);
        if (std::fabs(noseForward) > 0.0001f) {
            anchorPos.x += forwardAxis.x * noseForward;
            anchorPos.y += forwardAxis.y * noseForward;
            anchorPos.z += forwardAxis.z * noseForward;
        }

        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
        Systems::PseudoFPConfigManager::Get().GetOffsets(offsetX, offsetY, offsetZ);

        RE::NiPoint3 worldPos = anchorPos;

        worldPos.x += (-forwardAxis.x * offsetX) + (rightAxis.x * offsetY) + (upAxis.x * offsetZ);
        worldPos.y += (-forwardAxis.y * offsetX) + (rightAxis.y * offsetY) + (upAxis.y * offsetZ);
        worldPos.z += (-forwardAxis.z * offsetX) + (rightAxis.z * offsetY) + (upAxis.z * offsetZ);

        float debugX, debugY, debugZ;
        Systems::PseudoFPConfigManager::Get().GetDebugOffsets(debugX, debugY, debugZ);
        worldPos.x += debugX;
        worldPos.y += debugY;
        worldPos.z += debugZ;

        if (!ultraRigidHeadAttach) {
            if (usingFallback) {
                worldPos = ClampWorldPosToPlayerCapsule(player, worldPos, anchorPos);
            }
        }

        if (outHeadAnchor) {
            *outHeadAnchor = anchorPos;
        }
        if (outUsingFallback) {
            *outUsingFallback = usingFallback;
        }

        outWorldPos = worldPos;
        return true;
    }
}
