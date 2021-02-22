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

#include "DeviceCapabilities.h"
#include <stddef.h> // NULL

#define VID_ONTRAK      0x0a07
#define VID_PIDCODES    0x1209

#define PID_ADU200      200
#define PID_ADU208      208
#define PID_ADU218      218
#define PID_RELACON     0xFA70

struct DeviceTableEntry
{
    uint16_t vid;
    uint16_t pid;
    DeviceCapabilities capabilities;
};

struct DeviceTableEntry SUPPORTED_DEVICES[] =
{
    // OnTrak ADU200
    /* Not yet tested/supported
    {
        .vid = VID_ONTRAK,
        .pid = PID_ADU200,
        {
            .numInputs = 4,
            .numRelays = 4,
        }
    },
    */

    // OnTrak ADU208
    {
        .vid = VID_ONTRAK,
        .pid = PID_ADU208,
        {
            .numInputs = 8,
            .numRelays = 8,
        }
    },

    // OnTrak ADU218
    {
        .vid = VID_ONTRAK,
        .pid = PID_ADU218,
        {
            .numInputs = 8,
            .numRelays = 8,
        }
    },

    // Relacon Relay Controller
    {
        .vid = VID_PIDCODES,
        .pid = PID_RELACON,
        {
            .numInputs = 8,
            .numRelays = 8,
        }
    }
};

const DeviceCapabilities * DeviceCapabilitiesQuery(uint16_t vid, uint16_t pid)
{
    for (unsigned i = 0; i < sizeof(SUPPORTED_DEVICES) / sizeof(SUPPORTED_DEVICES[0]); i++)
    {
        struct DeviceTableEntry *entry = &SUPPORTED_DEVICES[i];
        if (entry->vid == vid && entry->pid == pid)
        {
            return &entry->capabilities;
        }
    }

    return NULL;
}