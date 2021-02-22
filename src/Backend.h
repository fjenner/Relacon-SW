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

#ifndef BACKEND_H
#define BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include "Relacon.h"

struct Log;

struct Backend;
struct BackendDevice;
struct BackendDeviceList;
struct BackendDeviceListEntry;

typedef struct Backend Backend;
typedef struct BackendDevice BackendDevice;
typedef struct BackendDeviceList BackendDeviceList;
typedef struct BackendDeviceListEntry BackendDeviceListEntry;

/**
 * Initializes this backend
 *
 * @param[in] log The log instance to use for messages from this backend
 * @param[out] backend Receives a pointer to the newly initialized backend on
 *                     success
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_OUT_OF_MEMORY could not allocate memory for the
 *                                            backend
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal error occurred
 */
RelaconStatus BackendInit(struct Log *log, Backend **backend);

/**
 * Cleans up the after the specified backend
 *
 * @param[in] backend The backend to clean up
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal error occurred
 */
RelaconStatus BackendExit(Backend *backend);

/**
 * Creates a list of potential devices to be opened by this backend
 *
 * @param[in] backend The backend instance
 * @param[out] devList Receives a pointer to the device list on success
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_OUT_OF_MEMORY there was insufficient memory to
 *                                            allocate the device list
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal error occurred
 */
RelaconStatus BackendDeviceListCreate(
    Backend *backend,
    BackendDeviceList **devList);

/**
 * Destroys the device list. Note that the lifetime of all device list entries
 * and their associated RelaconDeviceInfo and device handles are tied to the
 * lifetime of the list. Thus, use of any strings in the device info or use of
 * the handle to open a device must be done *before* the device list is
 * destroyed. Once the handle has been used to open the device, the list can
 * be safely destroyed.
 *
 * @param[in] devList The device list to destroy
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal error occurred
 */
RelaconStatus BackendDeviceListDestroy(BackendDeviceList *devList);

/**
 * Gets the device information for the next device in the list, or NULL if
 * there are no more items in the list. The lifetime of the returned object is
 * tied to the lifetime of the device list from which it was retrieved.
 *
 * @param[in] devList The device list to iterate through
 *
 * @retval Returns the device information for the next device, or NULL if there
 *         are no more devices in the device list
 */
const RelaconDeviceInfo * BackendDeviceListGetNext(BackendDeviceList *devList);

/**
 * Open the device represented by the specified handle
 *
 * @pre The device list from which the handle was retrieved must still be
 *      valid (not yet destroyed)
 *
 * @param[in] handle The handle corresponding to the device to open
 * @param[out] dev Receives a handle to an open device
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_OUT_OF_MEMORY there was insufficient memory to
 *                                            allocate the device
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal error occurred
 */
RelaconStatus BackendDeviceOpen(void *handle, BackendDevice **dev);

/**
 * Closes the specified device
 *
 * @param[in] dev The device to close
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal error occurred
 */
RelaconStatus BackendDeviceClose(BackendDevice *dev);

/**
 * Writes a HID report to the specified device. The provided buffer must
 * include the report ID in addition to the report data, and the length must
 * reflect the expected report size (plus report ID) for the targed device.
 *
 * @param[in] dev The device to write the report to
 * @param[in] buf The report buffer (including leading report ID)
 * @param[in] len The length of the report buffer (report ID + report data)
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_DEVICE_IO there was a problem communicating
 *                                        with the device
 */
RelaconStatus BackendDeviceWriteReport(
    BackendDevice *dev,
    const uint8_t *buf,
    size_t len);

/**
 * Reads a HID report from the specified device. The provided buffer must
 * include room for the report ID in addition to the report data.
 *
 * @param[in] dev The device to read the report from
 * @param[out] buf The report buffer to be populated (the first byte will be
 *                 populated with the report ID)
 * @param[in] len The length of the output buffer
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_DEVICE_IO there was a problem communicating
 *                                        with the device
 */
RelaconStatus BackendDeviceReadReport(
    BackendDevice *dev,
    uint8_t *buf,
    size_t len,
    unsigned timeoutMs);

#endif