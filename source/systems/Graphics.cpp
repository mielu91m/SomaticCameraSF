/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "systems/Graphics.h"

namespace Systems {

    Graphics::Graphics() {}

    bool Graphics::Setup()
    {
        if (m_Initialized)
            return true;

        m_Initialized = true;
        spdlog::info("Graphics initialized successfully");
        return true;
    }

    bool Graphics::Shutdown()
    {
        m_RenderTargetView.Reset();
        m_BackBuffer.Reset();
        m_Context.Reset();
        m_Device.Reset();
        m_Initialized = false;
        return true;
    }

    void Graphics::CreateRenderTarget()
    {
    }

    void Graphics::ClearRenderTarget()
    {
    }
}
