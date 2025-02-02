/*
Copyright 2021 Frank Jenner

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DEVICE_CAPABILITIES_H
#define DEVICE_CAPABILITIES_H

#include <stdint.h>

/**
 * The capabilities available on a supported device
 */
struct DeviceCapabilities
{
    /** The number of digital inputs */
    unsigned numInputs;

    /** The number of relays */
    unsigned numRelays;
};

typedef struct DeviceCapabilities DeviceCapabilities;

/**
 * Retrieves the capabilities for a supported relay controller device
 *
 * @param[in] vid The USB vendor ID
 * @param[in] pid The USB product ID
 *
 * @return Returns the capabilities of the device corresponding to the
 *         specified USB VID and PID, or NULL if the VID/PID does not
 *         correspond to a device supported by this software
 */
const DeviceCapabilities * DeviceCapabilitiesQuery(uint16_t vid, uint16_t pid);

#endif