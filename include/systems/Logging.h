/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

namespace Systems {

    class Logging {

    public:
        Logging();
        ~Logging() = default;

        Logging(const Logging&) = delete;
        Logging& operator=(const Logging&) = delete;
        Logging(Logging&&) = delete;
        Logging& operator=(Logging&&) = delete;

        void OpenRelative(const std::filesystem::path& path);

    private:
        std::shared_ptr<spdlog::logger> m_Logger;
    };
}
