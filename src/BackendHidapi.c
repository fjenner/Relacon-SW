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

#include "Backend.h"
#include "DeviceCapabilities.h"
#include "Log.h"
#include <stdlib.h>
#include <hidapi.h>

struct Backend
{
    struct Log *log;
};

struct BackendDevice
{
    Backend *backend;
    hid_device *hidDev;
};

struct BackendDeviceList
{
    Backend *backend;
    struct hid_device_info *hidapiListStart;
    BackendDeviceListEntry *listStart;
    BackendDeviceListEntry *listCurrent;
};

struct BackendDeviceListEntry
{
    BackendDeviceList *devList;
    RelaconDeviceInfo devInfo;
    BackendDeviceListEntry *next;
    const struct hid_device_info *hidDeviceInfo;
};

/**
 * Fetches a string descriptor from the hidapi and converts it to an ASCII
 * string.
 *
 * @param[in] dev The device from which to request the string
 * @param[in] get_string_fn Hidapi function that fetches the desired string
 * @param[out] buf Receives a pointer to a newly allocated string. The string
 *                 must be freed by the client. If the device does not have the
 *                 requested string descriptor (which is not an error), this
 *                 argument receives a value of NULL and this function returns
 *                 RELACON_STATUS_SUCCESS.
 *
 * @retval RELACON_STATUS_SUCCESS The operation completed successfully
 * @retval RELACON_STATUS_ERROR_OUT_OF_MEMORY There was insufficient memory to
 *                                            allocate the string
 * @retval RELACON_STATUS_ERROR_INTERNAL An error occurred during conversion of
 *                                       strings between encodings
 */
static RelaconStatus FetchString(
    Backend *backend,
    hid_device *dev,
    int (*get_string_fn)(hid_device *dev, wchar_t *buf, size_t len),
    char **buf)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    wchar_t strBuf[64];

    // Attempt to fetch the string descriptor through the hidapi accessor
    if (get_string_fn(dev, strBuf, sizeof(strBuf) / sizeof(strBuf[0])) == -1)
    {
        // There is no string descriptor; not an error
        *buf = NULL;
    }
    else
    {
        // Allocate an appropriately sized buffer for the ASCII string (assumes
        // the string data can be resepresented in ASCII encoding) + terminator
        size_t asciiStrBufLen = wcslen(strBuf) + 1;
        char *asciiStrBuf = malloc(asciiStrBufLen);
        if (asciiStrBuf == NULL)
        {
            LOG_ERROR(backend->log, "Failed to allocate memory for ASCII string\n");
            status = RELACON_STATUS_ERROR_OUT_OF_MEMORY;
        }
        else
        {
            size_t dstBytes = wcstombs(asciiStrBuf, strBuf, asciiStrBufLen);
            if (dstBytes == (size_t)-1)
            {
                LOG_WARNING(backend->log, "Failed to convert to ANSI string\n");
                status = RELACON_STATUS_ERROR_INTERNAL;
            }
            else
            {
                // Null-terminate the destination buffer if needed
                if (dstBytes == asciiStrBufLen)
                    asciiStrBuf[dstBytes - 1] = '\0';
                *buf = asciiStrBuf;
            }
        }
    }

    return status;
}

/**
 * Determines whether a particular device entry enumerated by the HIDAPI is a
 * potential candidate to be handled by this software.
 *
 * @param[in] devInfo The device information from the HIDAPI
 *
 * @return Returns true if the device has potential to be controlled by this
 *         software or false otherwise
 */
static bool IsPotentialRelaconDevice(const struct hid_device_info* devInfo)
{
    // We consider *any* HID device to be a potential Relacon device except
    // that on Windows we throw away any devices that have a collection usage
    // value which doesn't match the expected usage for the command/response
    // report collection (0x01)
#ifdef FILTER_HID_COLLECTION_USAGE
    return devInfo->usage == 0x01;
#else
    (void)devInfo;
    return true;
#endif
}

/**
 * Initializes the RelaconDevInfo corresponding to the provided list item
 *
 * @param[in] listEntry The list item whose device info to populate
 * @param[in] capabilities The device capabilities
 */
static void DeviceInfoPopulate(
    BackendDeviceListEntry *listEntry,
    const DeviceCapabilities *capabilities)
{
    RelaconDeviceInfo *devInfo = &listEntry->devInfo;
    Backend *backend = listEntry->devList->backend;

    devInfo->handle = listEntry;
    devInfo->vid = listEntry->hidDeviceInfo->vendor_id;
    devInfo->pid = listEntry->hidDeviceInfo->product_id;
    devInfo->product = NULL;
    devInfo->manufacturer = NULL;
    devInfo->serialNum = NULL;
    devInfo->numRelays = capabilities->numRelays;
    devInfo->numInputs = capabilities->numInputs;

    // Unfortunately, when the hidapi populates the hid_device_info structs, it
    // requests string descriptors using a length of 512 wchar_t's, resulting
    // in a request wLength of 0x0402 bytes. The problem is that the ADU218
    // firmware appears to treat the wLength values as 8 bits, so the request
    // for 0x0402 bytes is seen by the firmware as a request for 0x0002 bytes.
    // This request of 2 bytes merely returns the header portion of the string
    // descriptor, effectively presenting an empty string. Hence all the string
    // fields in the hid_device_info for the ADU218 are empty strings. To get
    // the actual strings, we must open the device and request the strings
    // using a smaller request length.
    hid_device *dev = hid_open_path(listEntry->hidDeviceInfo->path);

    if (dev == NULL)
    {
        LOG_WARNING(
            listEntry->devList->backend->log,
            "Failed to open device %s for querying string descriptors: %ls\n",
            listEntry->hidDeviceInfo->path,
            hid_error(NULL));
    }
    else
    {
        FetchString(backend, dev, hid_get_manufacturer_string, &devInfo->manufacturer);
        FetchString(backend, dev, hid_get_product_string, &devInfo->product);
        FetchString(backend, dev, hid_get_serial_number_string, &devInfo->serialNum);

        hid_close(dev);
    }
}

/**
 * Cleans up the memory (namely, the strings) associated with the specified
 * device list entry
 *
 * @param[in] listEntry The device list entry whose strings to clean up
 */
static void DeviceInfoDestroy(BackendDeviceListEntry *listEntry)
{
    RelaconDeviceInfo *devInfo = &listEntry->devInfo;
    free(devInfo->manufacturer);
    free(devInfo->product);
    free(devInfo->serialNum);
}

/**
 * Cleans up the list entries associated with the specified device list
 *
 * @param[in] devList The device list whose entries to clean up
 */
static void DeviceListEntriesDestroy(BackendDeviceList *devList)
{
    BackendDeviceListEntry *listEntry = devList->listStart;
    while (listEntry != NULL)
    {
        DeviceInfoDestroy(listEntry);
        BackendDeviceListEntry *tmp = listEntry;
        listEntry = listEntry->next;

        free(tmp);
    }
}

/**
 * Creates and populates the device list entries associated with the specified
 * newly allocated device list
 *
 * @param[in] devList The device list for which to create the list entries
 *
 * @retval RELACON_STATUS_SUCCESS The operation completed successfully
 * @retval RELACON_STATUS_ERROR_OUT_OF_MEMORY There was insufficient memory to
 *                                            allocate all the device entries
 */
RelaconStatus DeviceListEntriesCreate(BackendDeviceList *devList)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    struct Log *log = devList->backend->log;
    BackendDeviceListEntry *prevlistEntry = NULL;
    const struct hid_device_info *hidapiDevInfo = devList->hidapiListStart;

    while (hidapiDevInfo != NULL)
    {
        // Check if this is a recognized supported device
        const DeviceCapabilities *capabilities = DeviceCapabilitiesQuery(
            hidapiDevInfo->vendor_id,
            hidapiDevInfo->product_id);

        // Skip this entry if it is definitely not one we're interested in
        if (capabilities == NULL)
        {
            LOG_DEBUG(
                log,
                "Skipping unrecognized device %04x:%04x\n",
                hidapiDevInfo->vendor_id,
                hidapiDevInfo->product_id);
        }
        else if (!IsPotentialRelaconDevice(hidapiDevInfo))
        {
            LOG_DEBUG(
                log,
                "Skipping potential relacon device %04x:%04x with usage %02x\n",
                hidapiDevInfo->vendor_id,
                hidapiDevInfo->product_id,
                hidapiDevInfo->usage);
        }
        else
        {
            // Allocate a corresponding BackendDeviceListEntry accompanying the
            // HIDAPI device info
            BackendDeviceListEntry *listEntry = malloc(sizeof(BackendDeviceListEntry));

            if (listEntry == NULL)
            {
                LOG_ERROR(log, "Failed to allocate BackendDeviceListEntry\n");
                status = RELACON_STATUS_ERROR_OUT_OF_MEMORY;
                break;
            }

            // Populate the newly allocated private info
            listEntry->devList = devList;
            listEntry->hidDeviceInfo = hidapiDevInfo;
            listEntry->next = NULL;

            DeviceInfoPopulate(listEntry, capabilities);

            // Link the new node to the list
            if (prevlistEntry == NULL)
            {
                devList->listStart = listEntry;
            }
            else
            {
                prevlistEntry->next = listEntry;
            }
            prevlistEntry = listEntry;
        }

        hidapiDevInfo = hidapiDevInfo->next;
    }

    // On failure, clean up any entries that were already allocated
    if (status != RELACON_STATUS_SUCCESS)
    {
        DeviceListEntriesDestroy(devList);
    }

    return status;
}

RelaconStatus BackendInit(struct Log *log, Backend **backend)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;

    if (hid_init() != 0)
    {
        LOG_ERROR(log, "hid_init() failed\n");
        status = RELACON_STATUS_ERROR_INTERNAL;
    }
    else
    {
        Backend *hidapiBackend = malloc(sizeof(**backend));
        if (hidapiBackend == NULL)
        {
            LOG_ERROR(log, "Failed to allocate memory for HIDAPI backend\n");
            status = RELACON_STATUS_ERROR_OUT_OF_MEMORY;
            hid_exit();
        }
        else
        {
            hidapiBackend->log = log;
            *backend = hidapiBackend;
        }
    }

    return status;
}

RelaconStatus BackendExit(Backend *backend)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    struct Log *log = backend->log;

    free(backend);

    if (hid_exit() != 0)
    {
        LOG_ERROR(log, "hid_exit() failed\n");
        status = RELACON_STATUS_ERROR_INTERNAL;
    }

    return status;
}

RelaconStatus BackendDeviceListCreate(
    Backend *backend,
    BackendDeviceList **outList)
{
    (void) backend;
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    BackendDeviceList *devList = malloc(sizeof(**outList));

    if (devList == NULL)
    {
        status = RELACON_STATUS_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        devList->backend = backend;
        devList->listStart = NULL;
        devList->listCurrent = NULL;
        devList->hidapiListStart = hid_enumerate(0, 0);

        if (devList->hidapiListStart == NULL)
        {
            LOG_ERROR(backend->log, "hid_enumerate() returned NULL\n");
            status = RELACON_STATUS_ERROR_INTERNAL;

            free(devList);
        }
        else
        {
            status = DeviceListEntriesCreate(devList);

            if (status != RELACON_STATUS_SUCCESS)
            {
                hid_free_enumeration(devList->hidapiListStart);
                free(devList);
            }
            else
            {
                // Success!
                devList->listCurrent = devList->listStart;
                *outList = devList;
            }
        }
    }

    return status;
}

RelaconStatus BackendDeviceListDestroy(BackendDeviceList *devList)
{
    DeviceListEntriesDestroy(devList);
    hid_free_enumeration(devList->hidapiListStart);
    free(devList);

    return RELACON_STATUS_SUCCESS;
}

const RelaconDeviceInfo * BackendDeviceListGetNext(BackendDeviceList *devList)
{
    RelaconDeviceInfo *devInfo = NULL;
    BackendDeviceListEntry *listEntry = devList->listCurrent;

    // If we haven't hit the end of the list, set the device information to
    // return and advance the current list position for next time
    if (listEntry != NULL)
    {
        devInfo = &listEntry->devInfo;
        devList->listCurrent = devList->listCurrent->next;
    }

    return devInfo;
}

RelaconStatus BackendDeviceOpen(
    void *handle,
    BackendDevice **outDev)
{
    BackendDeviceListEntry *listEntry = handle;
    struct Log *log = listEntry->devList->backend->log;
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    BackendDevice *dev = malloc(sizeof(BackendDevice));

    if (dev == NULL)
    {
        LOG_ERROR(log, "Failed to allocate BackendDevice\n");
        status = RELACON_STATUS_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        dev->backend = listEntry->devList->backend;
        dev->hidDev = hid_open_path(listEntry->hidDeviceInfo->path);
        if (dev->hidDev == NULL)
        {
            LOG_ERROR(log, "hid_open_path failed: %ls\n", hid_error(NULL));
            status = RELACON_STATUS_ERROR_INTERNAL;
        }
        else
        {
            // Success!
            *outDev = dev;
        }
    }

    return status;
}

RelaconStatus BackendDeviceClose(BackendDevice *dev)
{
    hid_close(dev->hidDev);
    free(dev);

    return RELACON_STATUS_SUCCESS;
}

RelaconStatus BackendDeviceWriteReport(
    BackendDevice *dev,
    const uint8_t *buf,
    size_t len)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;

    int result = hid_write(dev->hidDev, buf, len);
    if (result == -1)
    {
        LOG_ERROR(dev->backend->log, "hid_write() failed: %ls\n", hid_error(dev->hidDev));
        status = RELACON_STATUS_ERROR_DEVICE_IO;
    }

    return status;
}

RelaconStatus BackendDeviceReadReport(
    BackendDevice *dev,
    uint8_t *buf,
    size_t len,
    unsigned timeoutMs)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    struct Log *log = dev->backend->log;

    int result = hid_read_timeout(dev->hidDev, buf, len, timeoutMs);
    if (result == -1)
    {
        LOG_ERROR(log, "hid_read() failed: %ls\n", hid_error(dev->hidDev));
        status = RELACON_STATUS_ERROR_DEVICE_IO;
    }
    else if (result == 0)
    {
        LOG_ERROR(log, "hid_read_timeout() timed out\n");
        status = RELACON_STATUS_ERROR_TIMEOUT;
    }

    return status;
}