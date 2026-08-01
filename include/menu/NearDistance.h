/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "menu/IMenu.h"

namespace Menu {

    class NearDistance : public IMenu {

    public:
        NearDistance() = default;
        ~NearDistance() override = default;

        std::string GetName() const override { return "Near Distance"; }
        void Draw() override;
    };
}
