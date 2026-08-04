/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "starfield/OsfUiIntegration.h"
#include "starfield/StarfieldSF.h"
#include "systems/Config.h"
#include "plugin.h"
#include "version.h"
#include <OSFUI_API.h>

#include <cstring>
#include <cstdio>
#include <ctime>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <Windows.h>

namespace Patch {

    OsfUiIntegration* OsfUiIntegration::s_Instance = nullptr;

    namespace {

        constexpr const char* kModId = "somaticcamera.sf";

        std::string GetIniPath()
        {
            char path[MAX_PATH];
            GetModuleFileNameA(GetModuleHandleA("SomaticCameraSF.dll"), path, MAX_PATH);
            std::string full(path);
            size_t pos = full.find_last_of("\\/");
            std::string dir = (pos != std::string::npos) ? full.substr(0, pos + 1) : "";
            return dir + "SomaticCameraSF.ini";
        }

        void WriteIniString(const char* a_section, const char* a_key, const char* a_value)
        {
            std::string path = GetIniPath();
            WritePrivateProfileStringA(a_section, a_key, a_value, path.c_str());
        }

        void Log(const char* fmt, ...)
        {
            char* userProfile = nullptr;
            size_t len = 0;
            _dupenv_s(&userProfile, &len, "USERPROFILE");
            std::string path = std::string(userProfile ? userProfile : "") + "\\Documents\\My Games\\Starfield\\SFSE\\SomaticCameraSF_debug.log";
            free(userProfile);
            std::ofstream log(path, std::ios::app);
            if (log) {
                time_t t = time(nullptr);
                struct tm tm;
                localtime_s(&tm, &t);
                char buf[64];
                strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
                log << "[OsfUI " << buf << "] ";
                char msg[768];
                va_list args;
                va_start(args, fmt);
                vsnprintf(msg, sizeof(msg), fmt, args);
                va_end(args);
                log << msg << std::endl;
            }
        }

        int KeyNameToVK(const std::string& keyName)
        {
            std::string key = keyName;
            for (auto& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

            if (key == "F1") return 0x70;
            if (key == "F2") return 0x71;
            if (key == "F3") return 0x72;
            if (key == "F4") return 0x73;
            if (key == "F5") return 0x74;
            if (key == "F6") return 0x75;
            if (key == "F7") return 0x76;
            if (key == "F8") return 0x77;
            if (key == "F9") return 0x78;
            if (key == "F10") return 0x79;
            if (key == "F11") return 0x7A;
            if (key == "F12") return 0x7B;

            if (key == "HOME") return 0x24;
            if (key == "END") return 0x25;
            if (key == "PAGEUP") return 0x21;
            if (key == "PAGEDOWN") return 0x23;
            if (key == "INSERT") return 0x2D;
            if (key == "DELETE") return 0x2E;
            if (key == "BACK") return 0x08;
            if (key == "TAB") return 0x09;
            if (key == "ENTER") return 0x0D;
            if (key == "ESCAPE") return 0x1B;
            if (key == "SPACE") return 0x20;
            if (key == "LEFT") return 0x25;
            if (key == "UP") return 0x26;
            if (key == "RIGHT") return 0x27;
            if (key == "DOWN") return 0x28;
            if (key == "CAPSLOCK") return 0x14;
            if (key == "LSHIFT" || key == "SHIFT") return 0xA0;
            if (key == "RSHIFT") return 0xA1;
            if (key == "LCONTROL" || key == "CTRL") return 0xA2;
            if (key == "RCONTROL") return 0xA3;
            if (key == "LMENU" || key == "ALT") return 0xA4;
            if (key == "RMENU") return 0xA5;

            if (key == "NMLOCK") return 0x90;
            if (key == "SCROLL") return 0x91;
            if (key == "DIVIDE") return 0x6F;
            if (key == "MULTIPLY") return 0x6A;
            if (key == "SUBTRACT") return 0x6D;
            if (key == "ADD") return 0x6B;
            if (key == "NUMPADENTER") return 0x0D;

            if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') return key[0];

            if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') return key[0];

            if (key == "NUMPAD0" || key == "NUM0" || key == "NUM PAD 0" || key == "NUMPAD.0" || key == "KP_0") return 0x60;
            if (key == "NUMPAD1" || key == "NUM1" || key == "NUM PAD 1" || key == "NUMPAD.1" || key == "KP_1") return 0x61;
            if (key == "NUMPAD2" || key == "NUM2" || key == "NUM PAD 2" || key == "NUMPAD.2" || key == "KP_2") return 0x62;
            if (key == "NUMPAD3" || key == "NUM3" || key == "NUM PAD 3" || key == "NUMPAD.3" || key == "KP_3") return 0x63;
            if (key == "NUMPAD4" || key == "NUM4" || key == "NUM PAD 4" || key == "NUMPAD.4" || key == "KP_4") return 0x64;
            if (key == "NUMPAD5" || key == "NUM5" || key == "NUM PAD 5" || key == "NUMPAD.5" || key == "KP_5") return 0x65;
            if (key == "NUMPAD6" || key == "NUM6" || key == "NUM PAD 6" || key == "NUMPAD.6" || key == "KP_6") return 0x66;
            if (key == "NUMPAD7" || key == "NUM7" || key == "NUM PAD 7" || key == "NUMPAD.7" || key == "KP_7") return 0x67;
            if (key == "NUMPAD8" || key == "NUM8" || key == "NUM PAD 8" || key == "NUMPAD.8" || key == "KP_8") return 0x68;
            if (key == "NUMPAD9" || key == "NUM9" || key == "NUM PAD 9" || key == "NUMPAD.9" || key == "KP_9") return 0x69;

            int num = std::atoi(key.c_str());
            if (num > 0 && num <= 255) return num;

            return 0;
        }

        std::string StripJsonString(const char* a_valueJson)
        {
            std::string s = a_valueJson;
            if (!s.empty() && s.front() == '"') {
                size_t end = s.find_last_of('"');
                if (end != std::string::npos && end > 0) {
                    s = s.substr(1, end - 1);
                }
            }
            return s;
        }

        void OnSettingChanged(const char* a_modId, const char* a_key, const char* a_valueJson, void* a_user) noexcept
        {
            (void)a_modId;
            auto* self = static_cast<OsfUiIntegration*>(a_user);
            if (!self) {
                return;
            }
            self->ApplySetting(a_key, a_valueJson);
        }

    }

    bool OsfUiIntegration::Init(Systems::Config* a_config)
    {
        s_Instance = this;
        m_Config = a_config;

        m_Bridge = OSFUI::API::RequestBridge();
        if (!m_Bridge) {
            Log("[OsfUiIntegration] OSF UI bridge not available");
            return false;
        }

        m_SettingsToken = m_Bridge->SubscribeSettings(kModId, OnSettingChanged, this);
        if (m_SettingsToken == 0) {
            Log("[OsfUiIntegration] Failed to subscribe to settings");
        } else {
            Log("[OsfUiIntegration] Subscribed to settings for %s", kModId);
        }

        if (m_Config && m_Config->PseudoFP()) {
            const auto* pfp = m_Config->PseudoFP();
            WriteIniString("PseudoFP", "fSideOffset", std::to_string(pfp->safOffsetSide).c_str());
            WriteIniString("PseudoFP", "fForwardOffset", std::to_string(pfp->safOffsetForward).c_str());
            WriteIniString("PseudoFP", "fUpOffset", std::to_string(pfp->safOffsetUp).c_str());
            WriteIniString("PseudoFP", "fFOV", std::to_string(pfp->fov).c_str());
            WriteIniString("Settings", "iToggleKey", std::to_string(pfp->toggleKey).c_str());
            WriteIniString("PseudoFP", "bAutoEquipHideHeadSS", pfp->autoEquipHideHeadSS ? "1" : "0");
            WriteIniString("PseudoFP", "bAutoEquipHideHead", pfp->autoEquipHideHead ? "1" : "0");
        }

        return true;
    }

    void OsfUiIntegration::Shutdown()
    {
        if (m_Bridge && m_SettingsToken != 0) {
            m_Bridge->UnsubscribeSettings(m_SettingsToken);
            m_SettingsToken = 0;
        }
        s_Instance = nullptr;
    }

    void OsfUiIntegration::ApplySetting(const char* a_key, const char* a_valueJson)
    {
        if (!m_Config) {
            return;
        }

        auto* cfg = m_Config;

        if (std::strcmp(a_key, "iToggleKey") == 0) {
            std::string keyName = StripJsonString(a_valueJson);
            Log("Toggle key received: keyName='%s' rawJson='%s'", keyName.c_str(), a_valueJson);
            int vk = KeyNameToVK(keyName);
            Log("Toggle key VK: %d keyName='%s'", vk, keyName.c_str());
            cfg->PseudoFP()->toggleKey = vk;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", vk);
            WriteIniString("Settings", "iToggleKey", buf);
        } else if (std::strcmp(a_key, "fFOV") == 0) {
            float val = static_cast<float>(std::atof(a_valueJson));
            cfg->PseudoFP()->fov = val;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%f", val);
            WriteIniString("PseudoFP", "fFOV", buf);
        } else if (std::strcmp(a_key, "bAutoEquipHideHeadSS") == 0) {
            bool val = false;
            if (std::strcmp(a_valueJson, "true") == 0) {
                val = true;
            }
            cfg->PseudoFP()->autoEquipHideHeadSS = val;
            WriteIniString("PseudoFP", "bAutoEquipHideHeadSS", val ? "1" : "0");
        } else if (std::strcmp(a_key, "bAutoEquipHideHead") == 0) {
            bool val = false;
            if (std::strcmp(a_valueJson, "true") == 0) {
                val = true;
            }
            cfg->PseudoFP()->autoEquipHideHead = val;
            WriteIniString("PseudoFP", "bAutoEquipHideHead", val ? "1" : "0");
        } else if (std::strcmp(a_key, "fSideOffset") == 0) {
            float val = static_cast<float>(std::atof(a_valueJson));
            cfg->PseudoFP()->safOffsetSide = val;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%f", val);
            WriteIniString("PseudoFP", "fSideOffset", buf);
        } else if (std::strcmp(a_key, "fUpOffset") == 0) {
            float val = static_cast<float>(std::atof(a_valueJson));
            cfg->PseudoFP()->safOffsetUp = val;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%f", val);
            WriteIniString("PseudoFP", "fUpOffset", buf);
        } else if (std::strcmp(a_key, "fForwardOffset") == 0) {
            float val = static_cast<float>(std::atof(a_valueJson));
            cfg->PseudoFP()->safOffsetForward = val;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%f", val);
            WriteIniString("PseudoFP", "fForwardOffset", buf);
        }
    }

}