/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

namespace RE
{
    class Actor;
}

namespace SAFIntegration
{
    /// Checks if the SAF (Starfield Animation Framework) DLL is loaded and
    /// currently playing an animation on the given actor.
    /// Returns false if SAF is not present or not animating the actor.
    bool IsSAFAnimationPlaying(RE::Actor* a_actor);
}
