/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <cstdint>
#include <string>

namespace Systems {

    struct PseudoFPConfig {
        float forwardOffset = 0.0f;
        float sideOffset = 0.0f;
        float upOffset = 0.0f;
        float noseForward = 0.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
        float debugOffsetX = 0.0f;
        float debugOffsetY = 0.0f;
        float debugOffsetZ = 0.0f;
        float maxYawRad = 1.57079633f;
        float ultraRigidMaxPlanarOffset = 0.60f;
        float ultraRigidHeightTolerance = 0.50f;
        float furnitureExitGraceFrames = 90.0f;
        float pseudoFPPFOV = 85.0f;
        uint32_t headCacheGraceFrames = 45;
        bool ultraRigidHeadAttach = false;
        bool ultraRigidIgnoreSanity = false;
        bool safOneToOneHead = true;
        bool safPitchSyncEnabled = false;
        float safOffsetForward = 0.0f;
        float safOffsetSide = 0.0f;
        float safOffsetUp = 0.0f;
        float cameraTransitionSeconds = 0.18f;
        int refreshCounter = 0;
        int saFRefreshCounter = 0;
    };

    class PseudoFPConfigManager {
    public:
        static PseudoFPConfigManager& Get();

        void EnsureCached();
        void Refresh();

        float GetForwardOffset();
        float GetSideOffset();
        float GetUpOffset();
        float GetNoseForward();
        float GetMaxYawRad();
        float GetUltraRigidMaxPlanarOffset();
        float GetUltraRigidHeightTolerance();
        float GetFurnitureExitGraceFrames();
        float GetPseudoFOV();
        uint32_t GetHeadCacheGraceFrames();
        bool IsUltraRigidEnabled();
        bool IsUltraRigidIgnoreSanityEnabled();
        bool IsSAFOneToOneHeadEnabled();
        bool IsSAFPitchSyncEnabled();
        void GetSAFOffsets(float& outForward, float& outSide, float& outUp);
        void GetOffsets(float& outX, float& outY, float& outZ);
        void GetDebugOffsets(float& outX, float& outY, float& outZ);
        float GetCameraTransitionSeconds();

    private:
        PseudoFPConfigManager() = default;
        PseudoFPConfig m_Cache;
        static constexpr int kRefreshInterval = 600;
        static constexpr int kSAFOffsetRefreshInterval = 30;
    };
}
