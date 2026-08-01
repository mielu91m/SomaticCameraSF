/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "systems/PseudoFPConfig.h"
#include <Windows.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace Systems {

    static std::string GetPluginDirIniPath()
    {
        static std::string iniPath;
        static bool initialized = false;
        if (!initialized) {
            char path[MAX_PATH];
            GetModuleFileNameA(GetModuleHandleA("SomaticCameraSF.dll"), path, MAX_PATH);
            std::string full(path);
            size_t pos = full.find_last_of("\\/");
            std::string dir = (pos != std::string::npos) ? full.substr(0, pos + 1) : "";
            iniPath = dir + "SomaticCameraSF.ini";
            initialized = true;
        }
        return iniPath;
    }

    static float GetIniFloat(const char* section, const char* key, float defaultValue)
    {
        const std::string& iniPath = GetPluginDirIniPath();
        char buf[32];
        GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), iniPath.c_str());
        if (strlen(buf) == 0)
            return defaultValue;
        return static_cast<float>(atof(buf));
    }

    static bool GetIniBool(const char* section, const char* key, bool defaultValue)
    {
        const std::string& iniPath = GetPluginDirIniPath();
        char buf[32];
        GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), iniPath.c_str());
        if (strlen(buf) == 0)
            return defaultValue;
        std::string value(buf);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value == "1" || value == "true" || value == "yes" || value == "on";
    }

    PseudoFPConfigManager& PseudoFPConfigManager::Get()
    {
        static PseudoFPConfigManager instance;
        return instance;
    }

    void PseudoFPConfigManager::Refresh()
    {
        m_Cache.forwardOffset = GetIniFloat("PseudoFP", "fForwardOffset",
            GetIniFloat("PseudoFP", "fNoseForward", 0.0f) - GetIniFloat("PseudoFP", "fOffsetX", 0.0f));
        m_Cache.sideOffset = GetIniFloat("PseudoFP", "fSideOffset",
            GetIniFloat("PseudoFP", "fOffsetY", 0.0f));
        m_Cache.upOffset = GetIniFloat("PseudoFP", "fUpOffset",
            GetIniFloat("PseudoFP", "fOffsetZ", 0.0f));
        m_Cache.noseForward = GetIniFloat("PseudoFP", "fNoseForward", 0.0f);
        m_Cache.offsetX = GetIniFloat("PseudoFP", "fOffsetX", 0.0f);
        m_Cache.offsetY = GetIniFloat("PseudoFP", "fOffsetY", 0.0f);
        m_Cache.offsetZ = GetIniFloat("PseudoFP", "fOffsetZ", 0.0f);
        m_Cache.debugOffsetX = GetIniFloat("PseudoFP", "fDebugOffsetX", 0.0f);
        m_Cache.debugOffsetY = GetIniFloat("PseudoFP", "fDebugOffsetY", 0.0f);
        m_Cache.debugOffsetZ = GetIniFloat("PseudoFP", "fDebugOffsetZ", 0.0f);
        m_Cache.ultraRigidHeadAttach = GetIniBool("PseudoFP", "bUltraRigidHeadAttach", false);
        m_Cache.ultraRigidIgnoreSanity = GetIniBool("PseudoFP", "bUltraRigidIgnoreSanityChecks", false);
        float configuredGrace = GetIniFloat("PseudoFP", "fHeadAttachGraceFrames", 45.0f);
        float clamped = (std::max)(0.0f, (std::min)(configuredGrace, 240.0f));
        m_Cache.headCacheGraceFrames = static_cast<uint32_t>(clamped + 0.5f);
        m_Cache.ultraRigidMaxPlanarOffset = (std::max)(0.20f, (std::min)(GetIniFloat("PseudoFP", "fUltraRigidMaxPlanarOffset", 0.60f), 1.50f));
        m_Cache.ultraRigidHeightTolerance = (std::max)(0.10f, (std::min)(GetIniFloat("PseudoFP", "fUltraRigidHeightTolerance", 0.50f), 3.50f));
        float maxYawDeg = GetIniFloat("PseudoFP", "fMaxYawDegrees", 90.0f);
        m_Cache.maxYawRad = (std::max)(30.0f, (std::min)(maxYawDeg, 180.0f)) * (3.14159265f / 180.0f);
        m_Cache.furnitureExitGraceFrames = GetIniFloat("PseudoFP", "fFurnitureExitGraceFrames", 90.0f);
        m_Cache.pseudoFPPFOV = GetIniFloat("PseudoFP", "fFOV", 85.0f);
        // When true (default), the pseudo camera during SAF scenes sits
        // exactly at the head bone's world position (true 1:1 tracking),
        // regardless of whether the actor is standing, prone, or supine.
        // When false, a small eye offset is added below - but that offset
        // is expressed along the head bone's OWN local axes (so it rotates
        // correctly with the head in every pose), not the old world-Z-locked
        // frame that caused the camera to land above/behind/in front of the
        // head depending on posture.
        m_Cache.safOneToOneHead = GetIniBool("PseudoFP", "bSAF1to1HeadPosition", true);
        // Off by default: the camera-rotation pitch-sync feature has gone
        // through several blind attempts at guessing this engine's actual
        // NiCamera/cameraRoot rotation-matrix axis convention, all wrong
        // in different ways (confirmed by repeated in-game testing), most
        // recently causing a hard 90 deg yaw error at SAF start. Rather
        // than guess a fourth time without being able to compile/test,
        // this stays OFF by default (falling back to the confirmed-good
        // position-only 1:1 head tracking) until it's fixed with real
        // logged matrix data instead of assumptions. Set to 1 to
        // re-enable and help gather that data - see SAF_ROT_RAW in the
        // debug log.
        m_Cache.safPitchSyncEnabled = GetIniBool("PseudoFP", "bSAFPitchSync", false);
        m_Cache.safOffsetForward = GetIniFloat("PseudoFP", "fSAFOffsetForward", 0.12f);
        m_Cache.safOffsetSide = GetIniFloat("PseudoFP", "fSAFOffsetSide", 0.0f);
        m_Cache.safOffsetUp = GetIniFloat("PseudoFP", "fSAFOffsetUp", 0.0f);
        // Duration (ms) of the smoothed position blend applied whenever the
        // camera switches between the normal (TPP/vanity, "/") camera and
        // the pseudo-first-person rig (toggle key, default F4). 0 disables
        // smoothing and restores the old instant-cut behaviour. Clamped to
        // a sane range so a bad INI value can't leave the camera stuck
        // mid-blend or introduce a visible multi-second "floaty" delay.
        float transitionMs = GetIniFloat("PseudoFP", "fCameraTransitionMs", 180.0f);
        transitionMs = (std::max)(0.0f, (std::min)(transitionMs, 600.0f));
        m_Cache.cameraTransitionSeconds = transitionMs / 1000.0f;
    }

    void PseudoFPConfigManager::EnsureCached()
    {
        if (m_Cache.refreshCounter <= 0) {
            Refresh();
            m_Cache.refreshCounter = kRefreshInterval;
        }
        m_Cache.refreshCounter--;
    }

    float PseudoFPConfigManager::GetForwardOffset() { EnsureCached(); return m_Cache.forwardOffset; }
    float PseudoFPConfigManager::GetSideOffset() { EnsureCached(); return m_Cache.sideOffset; }
    float PseudoFPConfigManager::GetUpOffset() { EnsureCached(); return m_Cache.upOffset; }
    float PseudoFPConfigManager::GetNoseForward() { EnsureCached(); return m_Cache.noseForward; }
    float PseudoFPConfigManager::GetMaxYawRad() { EnsureCached(); return m_Cache.maxYawRad; }
    float PseudoFPConfigManager::GetUltraRigidMaxPlanarOffset() { EnsureCached(); return m_Cache.ultraRigidMaxPlanarOffset; }
    float PseudoFPConfigManager::GetUltraRigidHeightTolerance() { EnsureCached(); return m_Cache.ultraRigidHeightTolerance; }
    float PseudoFPConfigManager::GetFurnitureExitGraceFrames() { EnsureCached(); return m_Cache.furnitureExitGraceFrames; }
    float PseudoFPConfigManager::GetPseudoFOV() { EnsureCached(); return m_Cache.pseudoFPPFOV; }
    uint32_t PseudoFPConfigManager::GetHeadCacheGraceFrames() { EnsureCached(); return m_Cache.headCacheGraceFrames; }
    bool PseudoFPConfigManager::IsUltraRigidEnabled() { EnsureCached(); return m_Cache.ultraRigidHeadAttach; }
    bool PseudoFPConfigManager::IsUltraRigidIgnoreSanityEnabled() { EnsureCached(); return m_Cache.ultraRigidIgnoreSanity; }
    bool PseudoFPConfigManager::IsSAFOneToOneHeadEnabled() { EnsureCached(); return m_Cache.safOneToOneHead; }
    bool PseudoFPConfigManager::IsSAFPitchSyncEnabled() { EnsureCached(); return m_Cache.safPitchSyncEnabled; }

    void PseudoFPConfigManager::GetSAFOffsets(float& outForward, float& outSide, float& outUp)
    {
        if (m_Cache.saFRefreshCounter <= 0) {
            m_Cache.safOffsetForward = GetIniFloat("PseudoFP", "fSAFOffsetForward", 0.0f);
            m_Cache.safOffsetSide = GetIniFloat("PseudoFP", "fSAFOffsetSide", 0.0f);
            m_Cache.safOffsetUp = GetIniFloat("PseudoFP", "fSAFOffsetUp", 0.0f);
            m_Cache.saFRefreshCounter = kSAFOffsetRefreshInterval;
        }
        m_Cache.saFRefreshCounter--;
        outForward = m_Cache.safOffsetForward;
        outSide = m_Cache.safOffsetSide;
        outUp = m_Cache.safOffsetUp;
    }

    void PseudoFPConfigManager::GetOffsets(float& outX, float& outY, float& outZ)
    {
        EnsureCached();
        outX = m_Cache.offsetX;
        outY = m_Cache.offsetY;
        outZ = m_Cache.offsetZ;
    }

    float PseudoFPConfigManager::GetCameraTransitionSeconds() { EnsureCached(); return m_Cache.cameraTransitionSeconds; }

    void PseudoFPConfigManager::GetDebugOffsets(float& outX, float& outY, float& outZ)
    {
        EnsureCached();
        outX = m_Cache.debugOffsetX;
        outY = m_Cache.debugOffsetY;
        outZ = m_Cache.debugOffsetZ;
    }
}
