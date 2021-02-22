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
#include <libusb.h>

#define STRING_DESCRIPTOR_BUF_LEN   128
#define ENDPOINT_HID_INPUT_REPORT   0x81
#define ENDPOINT_HID_OUTPUT_REPORT  0x01

struct Backend
{
    struct Log *log;
    libusb_context *libusbContext;
};

struct BackendDeviceList
{
    Backend *backend;
    libusb_device **deviceList;
    BackendDeviceListEntry *listEntryFirst;
    BackendDeviceListEntry *listEntryCurrent;
    unsigned deviceCount;
};

struct BackendDeviceListEntry
{
    BackendDeviceList *devList;
    BackendDeviceListEntry *next;
    RelaconDeviceInfo devInfo;
    libusb_device *device;
};

struct BackendDevice
{
    Backend *backend;
    libusb_device_handle *handle;
};

/**
 * Fetches the requested string descriptor for a device. This function
 * allocates memory for the resulting string and the client is responsible for
 * freeing it.
 *
 * @param[in] entry The device entry for which the descriptor is being fetched
 * @param[in] handle The device from which to fetch the string descriptor
 * @param[in] index The index of the descriptor to fetch
 * @param[out] str Receives a pointer to a newly allocated string
 *
 * @retval RELACON_STATUS_SUCCESS The operation completed successfully
 * @retval RELACON_STATUS_ERROR_OUT_OF_MEMORY There was insufficient memory to
 *                                            allocate for the requested string
 * @retval RELACON_STATUS_ERROR_NO_ENTRY There is no string descriptor for the
 *                                       specified index
 */
static RelaconStatus FetchStringDescriptor(
    BackendDeviceListEntry *entry,
    libusb_device_handle *handle,
    unsigned index,
    char **str)
{
    struct Log *log = entry->devList->backend->log;

    if (index == 0)
    {
        LOG_WARNING(log, "No string descriptor provided\n");
        return RELACON_STATUS_ERROR_NO_ENTRY;
    }

    RelaconStatus status = RELACON_STATUS_SUCCESS;

    char *newString = malloc(STRING_DESCRIPTOR_BUF_LEN);
    if (newString == NULL)
    {
        LOG_ERROR(log, "Failed to allocate memory for string descriptor\n");
        status = RELACON_STATUS_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        int result = libusb_get_string_descriptor_ascii(
            handle,
            index,
            (unsigned char*)newString,
            STRING_DESCRIPTOR_BUF_LEN);

        if (result < 0)
        {
            LOG_ERROR(log, "Failed to fetch string descriptor (index %u)\n", index);
            status = RELACON_STATUS_ERROR_INTERNAL;
            free(newString);
        }
        else
        {
            *str = newString;
        }
    }

    return status;
}

/**
 * Frees the strings associated with the specified backed device list entry
 *
 * @param[in] entry The entry whose device info contents to clean up
 */
static void DeviceInfoFree(BackendDeviceListEntry *entry)
{
    free(entry->devInfo.manufacturer);
    free(entry->devInfo.product);
    free(entry->devInfo.serialNum);
}

/**
 * Populates the string descriptor fields in the device info structure
 *
 * @param[in] entry The entry whose device information structure to populate
 *
 * @retval RELACON_STATUS_SUCCESS The operation completed successfully
 * @retval RELACON_STATUS_ERROR_OUT_OF_MEMORY There was insufficient memory to
 *                                            allocate the strings
 * @retval RELACON_STATUS_ERROR_INTERNAL The device could not be opened to
 *                                       query the device information
 */
static RelaconStatus PopulateDeviceInfoStrings(BackendDeviceListEntry *entry)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    RelaconDeviceInfo *devInfo = &entry->devInfo;
    
    // Get the device descriptor
    struct libusb_device_descriptor deviceDescriptor;
    libusb_get_device_descriptor(entry->device, &deviceDescriptor);

    // Open the device to read the string descriptors
    libusb_device_handle *handle;

    int result = libusb_open(entry->device, &handle);
    if (result != 0)
    {
        LOG_ERROR(
            entry->devList->backend->log,
            "libusb_open failed (%04x:%04x): %s\n",
            devInfo->vid,
            devInfo->pid,
            libusb_strerror(result));
        status = RELACON_STATUS_ERROR_INTERNAL;
    }
    else
    {
        // Array of string descriptors we want to fetch
        struct StringFetchInfo
        {
            unsigned stringIndex;
            char **dest;
        } stringFetchInfos[] =
        {
            { deviceDescriptor.iManufacturer, &devInfo->manufacturer },
            { deviceDescriptor.iProduct, &devInfo->product },
            { deviceDescriptor.iSerialNumber, &devInfo->serialNum }
        };

        // Fetch all the string descriptors
        const unsigned NUM_STRS = sizeof(stringFetchInfos) / sizeof(stringFetchInfos[0]);
        for (unsigned i = 0; status == RELACON_STATUS_SUCCESS && i < NUM_STRS; i++)
        {
            RelaconStatus fetchStatus = FetchStringDescriptor(
                entry,
                handle,
                stringFetchInfos[i].stringIndex,
                stringFetchInfos[i].dest);

            // It's okay if no such string descriptor exists; other failures
            // are not okay and should be propagated up the call stack
            if (fetchStatus != RELACON_STATUS_SUCCESS &&
                fetchStatus != RELACON_STATUS_ERROR_NO_ENTRY)
            {
                DeviceInfoFree(entry);
                status = fetchStatus;
            }
        }

        libusb_close(handle);
    }

    return status;
}

/**
 * Creates a single backend device entry corresponding to the provided libusb
 * device
 *
 * @param[in] devList The device list from which this entry originates
 * @param[in] libusbDevice The libusb device that the new entry corresponds to
 * @param[in] deviceDescriptor The USB device descriptor information
 * @param[in] capabilities The Relacon-compatible device capabilities
 * @param[out] entry Receives the new backend device entry on success
 *
 * @retval RELACON_STATUS_SUCCESS The operation completed successfully
 * @retval RELACON_STATUS_ERROR_OUT_OF_MEMORY There was insufficient memory to
 *                                            allocate the device
 * @retval RELACON_STATUS_ERROR_INTERNAL The device could not be opened
 */
static RelaconStatus DeviceListEntryCreate(
    BackendDeviceList *devList,
    libusb_device *libusbDevice,
    const struct libusb_device_descriptor *deviceDescriptor,
    const DeviceCapabilities *capabilities,
    BackendDeviceListEntry **entry)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    struct Log *log = devList->backend->log;

    // Allocate a backend context structure corresponding to this libusb device
    BackendDeviceListEntry *newEntry = malloc(sizeof(BackendDeviceListEntry));

    if (newEntry == NULL)
    {
        LOG_ERROR(log, "Failed to allocate memory for device list entry\n");
        status = RELACON_STATUS_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        newEntry->devList = devList;
        newEntry->device = libusbDevice; 

        RelaconDeviceInfo *devInfo = &newEntry->devInfo;
        devInfo->vid = deviceDescriptor->idVendor;
        devInfo->pid = deviceDescriptor->idProduct;
        devInfo->manufacturer = NULL;
        devInfo->product = NULL;
        devInfo->serialNum = NULL;
        devInfo->handle = newEntry;
        devInfo->numRelays = capabilities->numRelays;
        devInfo->numInputs = capabilities->numInputs;

        // Populating the strings actually requires communication with the
        // device, so we'll do that in a separate function
        status = PopulateDeviceInfoStrings(newEntry);

        if (status != RELACON_STATUS_SUCCESS)
        {
            LOG_ERROR(
                log,
                "Failed to fetch device strings for device %04x:%04x\n",
                devInfo->vid,
                devInfo->pid);
            free(newEntry);
        }
        else
        {
            *entry = newEntry;
        }
    }

    return status;
}

/**
 * Walks through the list of backend device entries, freeing the strings
 * within the entries and the entries themselves. Does NOT free the device
 * list itself.
 *
 * @param[in] devList The device list to destroy
 */
static void DeviceListEntriesDestroy(BackendDeviceList *devList)
{
    BackendDeviceListEntry *entry = devList->listEntryFirst;
    while (entry != NULL)
    {
        DeviceInfoFree(entry);
        BackendDeviceListEntry *tmp = entry->next;
        free(entry);
        entry = tmp;
    }
}

/**
 * Creates the corresponding BackendDeviceListEntry objects for each of the
 * devices in the libusb device list and strings them together into a linked
 * list.
 *
 * @param[in] devList The backend device list maintaining the pointer to the
 *                     head of the list
 *
 * @retval RELACON_STATUS_SUCCESS The operation succeeded
 * @retval RELACON_STATUS_ERROR_OUT_OF_MEMORY There was insufficient memory to
 *                                      create all of the device list entries
 */
static RelaconStatus DeviceListEntriesCreate(BackendDeviceList *devList)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    BackendDeviceListEntry *prevEntry = NULL;
    struct Log *log = devList->backend->log;

    // Iterate over all the devices in the list
    for (unsigned i = 0; i < devList->deviceCount; i++)
    {
        // Extract the device descriptor information
        libusb_device *libusbDevice = devList->deviceList[i];
        struct libusb_device_descriptor deviceDescriptor;
        libusb_get_device_descriptor(libusbDevice, &deviceDescriptor);

        // Skip over this device if it's not a recognized device
        const DeviceCapabilities *capabilities = DeviceCapabilitiesQuery(
            deviceDescriptor.idVendor,
            deviceDescriptor.idProduct);
        
        if (capabilities == NULL)
        {
            LOG_INFO(
                log,
                "Skipping over unrecognized USB device %04x:%04x\n",
                deviceDescriptor.idVendor,
                deviceDescriptor.idProduct);
            continue;
        }

        // Create an entry for this device
        BackendDeviceListEntry *entry;
        status = DeviceListEntryCreate(
            devList,
            libusbDevice,
            &deviceDescriptor,
            capabilities,
            &entry);

        if (status == RELACON_STATUS_SUCCESS)
        {
            // Link the previous list node to the new one
            entry->next = NULL;
            if (prevEntry != NULL)
            {
                prevEntry->next = entry;
            }
            else
            {
                devList->listEntryFirst = entry;
                devList->listEntryCurrent = entry;
            }
            prevEntry = entry;
        }
        else
        {
            // Creating this device failed, but that doesn't necessarily mean
            // we should stop. The current device might just be in use or
            // otherwise inaccessible. Continue attempting to go through the
            // list. The exception is that we should bail immediately if we're
            // out of memory.
            if (status == RELACON_STATUS_ERROR_OUT_OF_MEMORY)
            {
                LOG_ERROR(log, "Out of memory. Bailing device list creation\n");
                DeviceListEntriesDestroy(devList);
                break;
            }
            else
            {
                // Treat other errors as okay and keep on going through the
                // device list
                status = RELACON_STATUS_SUCCESS;
            }
        }
    }

    return status;
}

RelaconStatus BackendInit(struct Log *log, Backend **backend)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    Backend *newBackend = malloc(sizeof(Backend));

    if (newBackend == NULL)
    {
        LOG_ERROR(log, "Failed to allocate memory for backend\n");
        status = RELACON_STATUS_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        int result = libusb_init(&newBackend->libusbContext);

        if (result != 0)
        {
            LOG_ERROR(log, "libusb_init() failed: %s\n", libusb_strerror(result));
            status = RELACON_STATUS_ERROR_INTERNAL;
            free(newBackend);
        }
        else
        {
            newBackend->log = log;
            *backend = newBackend;
        }
    }

    return status;
}

RelaconStatus BackendExit(Backend *backend)
{
    libusb_exit(backend->libusbContext);
    free(backend);

    return RELACON_STATUS_SUCCESS;
}

RelaconStatus BackendDeviceListCreate(
    Backend *backend,
    BackendDeviceList **devList)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    struct Log *log = backend->log;
    BackendDeviceList *newDeviceList = malloc(sizeof(BackendDeviceList));

    if (newDeviceList == NULL)
    {
        LOG_ERROR(log, "Failed to allocate memory for device list\n");
        status = RELACON_STATUS_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        ssize_t result = libusb_get_device_list(
            backend->libusbContext,
            &newDeviceList->deviceList);

        if (result < 0)
        {
            LOG_ERROR(log, "libusb_get_device_list() failed: %s\n", libusb_strerror(result));
            status = RELACON_STATUS_ERROR_INTERNAL;
            free(newDeviceList);
        }
        else
        {
            newDeviceList->backend = backend;
            newDeviceList->deviceCount = (unsigned)result;
            newDeviceList->listEntryFirst = NULL;
            newDeviceList->listEntryCurrent = NULL;

            status = DeviceListEntriesCreate(newDeviceList);

            if (status != RELACON_STATUS_SUCCESS)
            {
                LOG_ERROR(log, "Failed to create device list entries\n");
            }
            else
            {
                // Success
                *devList = newDeviceList;
            }
        }
    }

    return status;
}

RelaconStatus BackendDeviceListDestroy(BackendDeviceList *devList)
{
    DeviceListEntriesDestroy(devList);
    libusb_free_device_list(devList->deviceList, 1);
    free(devList);

    return RELACON_STATUS_SUCCESS;
}

const RelaconDeviceInfo * BackendDeviceListGetNext(BackendDeviceList *devList)
{
    RelaconDeviceInfo *devInfo = NULL;

    if (devList->listEntryCurrent != NULL)
    {
        devInfo = &devList->listEntryCurrent->devInfo;
        devList->listEntryCurrent = devList->listEntryCurrent->next;
    }

    return devInfo;
}

RelaconStatus BackendDeviceOpen(void *handle, BackendDevice **dev)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;
    BackendDeviceListEntry *entry = (BackendDeviceListEntry*)handle;
    struct Log *log = entry->devList->backend->log;
    BackendDevice *newDev = malloc(sizeof(BackendDevice));

    if (newDev == NULL)
    {
        LOG_ERROR(log, "Failed to allocate memory for backend device\n");
        status = RELACON_STATUS_ERROR_OUT_OF_MEMORY;
    }
    else
    {
        newDev->backend = entry->devList->backend;
        int result = libusb_open(entry->device, &newDev->handle);

        if (result != 0)
        {
            LOG_ERROR(log, "libusb_open() failed (%s)\n", libusb_strerror(result));
            status = RELACON_STATUS_ERROR_INTERNAL;
            free(newDev);
        }
        else
        {
            libusb_set_auto_detach_kernel_driver(newDev->handle, 1);

            // Claim the interface for the HID device (will fail if interface
            // is already in use)
            result = libusb_claim_interface(newDev->handle, 0);

            if (result != 0)
            {
                LOG_ERROR(log, "Failed to claim interface: %s\n", libusb_strerror(result));
                status = RELACON_STATUS_ERROR_INTERNAL;
            }
            else
            {
                // Success!
                *dev = newDev;
            }
        }
    }

    return status;
}

RelaconStatus BackendDeviceClose(BackendDevice *dev)
{
    libusb_close(dev->handle);
    free(dev);

    return RELACON_STATUS_SUCCESS;
}

RelaconStatus BackendDeviceWriteReport(
    BackendDevice *dev,
    const uint8_t *buf,
    size_t len)
{
    RelaconStatus status = RELACON_STATUS_SUCCESS;

    // libusb_interrupt_transfer() takes a non-const buffer because it can be
    // used to transfer data in either direction. For OUT transfers, it should
    // not modify the buffer, so casting away the const should be safe here.
    unsigned char *data = (unsigned char*)buf;

    int result = libusb_interrupt_transfer(
        dev->handle,
        ENDPOINT_HID_OUTPUT_REPORT,
        data,
        len,
        NULL,
        0);
    
    if (result != 0)
    {
        LOG_ERROR(
            dev->backend->log,
            "Interrupt OUT transfer failed (%s)\n",
            libusb_strerror(result));
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

    int result = libusb_interrupt_transfer(
        dev->handle,
        ENDPOINT_HID_INPUT_REPORT,
        buf,
        len,
        NULL,
        timeoutMs);
    
    if (result != 0)
    {
        LOG_ERROR(
            dev->backend->log,
            "Interrupt IN transfer failed (%s)\n",
            libusb_strerror(result));
        status = RELACON_STATUS_ERROR_DEVICE_IO;
    }

    return status;
}
