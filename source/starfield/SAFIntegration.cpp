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

#include "starfield/SAFIntegration.h"
#include "starfield/HeadTracking.h"
#include <Windows.h>

namespace SAFIntegration
{
    // Import function pointer type for SAF's IsPlayingAnimation
    using IsPlayingAnimationFunc = bool (*)(RE::Actor*);

    static IsPlayingAnimationFunc s_IsPlayingAnimation = nullptr;

    /// Attempts to resolve SAF API function pointers.
    /// Returns true if all required functions are available.
    ///
    /// NOTE: must keep retrying every call. SFSE permanent tasks can start
    /// running before StarfieldAnimationFramework.dll has been loaded into
    /// the process, so the very first ResolveSAFApi() here can see
    /// GetModuleHandleA() return NULL and leave s_IsPlayingAnimation == nullptr
    /// forever. The old early-out `if (s_IsPlayingAnimation) return true;`
    /// permanently cached that failure: SAF would actually load moments
    /// later, play its animation, but we could never detect it - so the
    /// camera stayed on the (un-animated) resting head copy and "froze" in
    /// the head while the body animated. We now only short-circuit once the
    /// pointer is genuinely resolved; until then we re-query the module each
    /// time so we pick SAF up as soon as it appears.
    static bool ResolveSAFApi()
    {
        if (s_IsPlayingAnimation)
            return true;  // already resolved successfully

        HMODULE safModule = GetModuleHandleA("StarfieldAnimationFramework.dll");
        if (!safModule)
            return false;  // SAF not loaded yet - will retry next call

        s_IsPlayingAnimation = reinterpret_cast<IsPlayingAnimationFunc>(
            GetProcAddress(safModule, "SAFAPI_IsPlayingAnimation"));

        if (s_IsPlayingAnimation) {
            Patch::LogFormatted("SAFIntegration: resolved SAFAPI_IsPlayingAnimation=%p", (void*)s_IsPlayingAnimation);
        } else {
            Patch::LogFormatted("SAFIntegration: StarfieldAnimationFramework.dll loaded but SAFAPI_IsPlayingAnimation export NOT found");
        }

        return s_IsPlayingAnimation != nullptr;
    }

    bool IsSAFAnimationPlaying(RE::Actor* a_actor)
    {
        if (!a_actor)
            return false;

        if (!ResolveSAFApi())
            return false;

        return s_IsPlayingAnimation(a_actor);
    }
}
