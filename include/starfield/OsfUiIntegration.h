/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <cstdint>

namespace OSFUI::API {
    struct IOSFUIBridge;
}

namespace Systems {
    class Config;
}

namespace Patch {

    class OsfUiIntegration {

    public:
        OsfUiIntegration() = default;
        ~OsfUiIntegration() = default;

        OsfUiIntegration(const OsfUiIntegration&) = delete;
        OsfUiIntegration& operator=(OsfUiIntegration&) = delete;
        OsfUiIntegration(OsfUiIntegration&&) = delete;
        OsfUiIntegration& operator=(OsfUiIntegration&&) = delete;

        static OsfUiIntegration* Get() { return s_Instance; }

        bool Init(Systems::Config* a_config);
        void Shutdown();
        void ApplySetting(const char* a_key, const char* a_valueJson);

    private:
        static OsfUiIntegration* s_Instance;

        OSFUI::API::IOSFUIBridge* m_Bridge{ nullptr };
        Systems::Config* m_Config{ nullptr };
        std::uint32_t m_SettingsToken{ 0 };
    };

}