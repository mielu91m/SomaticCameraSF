/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <cstdint>
#include <unordered_map>

namespace Patch {

    class Addresses {

    public:
        Addresses();
        ~Addresses() = default;

        bool Load();

        uintptr_t GetAddress(const std::string& name) const;
        void SetAddress(const std::string& name, uintptr_t address);

    private:
        std::unordered_map<std::string, uintptr_t> m_Addresses;
        bool m_Loaded = false;
    };
}
