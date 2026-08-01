/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "RE/N/NiAVObject.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiMatrix3.h"
#include "RE/N/NiPoint.h"

namespace Patch::SAFAnimation
{
    // === Rotation sync ===
    void ApplyRotationSync(RE::NiAVObject* cr, RE::NiAVObject* niCam, bool recompute);
    RE::NiMatrix3 ComputeRigidEyeWorldRotation();
    float ComputeHeadPitchRadians();
    RE::NiMatrix3 BuildYawPreservingPitchRotation(const RE::NiMatrix3& currentRot, float targetPitchRad);

    // === State ===
    extern bool g_HasPreSAFRotation;
    extern RE::NiMatrix3 g_PreSAFRotation;
}
