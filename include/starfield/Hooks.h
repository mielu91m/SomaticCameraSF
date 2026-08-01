/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "starfield/HeadTracking.h"
#include "starfield/PseudoFP.h"
#include "systems/PseudoFPConfig.h"
#include "RE/T/TESCamera.h"
#include "RE/N/NiCamera.h"

namespace Patch {
    extern bool g_PseudoFPPActive;
    extern bool g_SAFAnimationPlaying;

    class Hooks {
    public:
        Hooks();
        ~Hooks() = default;

        Hooks(const Hooks&) = delete;
        Hooks& operator=(const Hooks&) = delete;
        Hooks(Hooks&&) = delete;
        Hooks& operator=(Hooks&&) = delete;

        bool Setup();
        bool Remove();

    private:
        bool m_Setup = false;
    };
}
