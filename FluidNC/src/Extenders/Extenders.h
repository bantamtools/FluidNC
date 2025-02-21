// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "../Configuration/GenericFactory.h"
#include "PinExtender.h"

namespace Extenders {
    class Extenders : public Configuration::Configurable {
    public:
        Extenders();

        PinExtender* _pinDrivers[10];

        uint8_t _i2c_num = 0;

        void validate() override;
        void group(Configuration::HandlerBase& handler) override;
        void init();

        ~Extenders();
    };

    using PinExtenderFactory = Configuration::GenericFactory<PinExtenderDriver>;
}
