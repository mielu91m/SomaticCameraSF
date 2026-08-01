/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <windows.h>

namespace Systems {

    class Window {

    public:
        Window();
        ~Window() = default;

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        bool Setup();
        bool Shutdown();

        HWND GetHandle() const { return m_Handle; }

    private:
        HWND m_Handle = nullptr;
        bool m_Initialized = false;
    };
}
