/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace Systems {

    class Graphics {

    public:
        Graphics();
        ~Graphics() = default;

        Graphics(const Graphics&) = delete;
        Graphics& operator=(const Graphics&) = delete;
        Graphics(Graphics&&) = delete;
        Graphics& operator=(Graphics&&) = delete;

        bool Setup();
        bool Shutdown();

        ID3D11Device* GetDevice() const { return m_Device.Get(); }
        ID3D11DeviceContext* GetContext() const { return m_Context.Get(); }
        IDXGISwapChain* GetSwapChain() const { return m_SwapChain.Get(); }
        ID3D11RenderTargetView* GetRenderTargetView() const { return m_RenderTargetView.Get(); }

        void CreateRenderTarget();
        void ClearRenderTarget();

    private:
        Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_Context;
        Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_BackBuffer;

        bool m_Initialized = false;
    };
}
