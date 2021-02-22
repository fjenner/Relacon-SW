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

#include "Relacon.h"
#include "Backend.h"
#include "Log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// The report buffer includes both the report ID byte and the report data
#define REPORT_DATA_LEN 7
#define REPORT_BUF_LEN  (REPORT_DATA_LEN + 1)
#define REPORT_ID_CMD   1

#define DEFAULT_TIMEOUT_MS 500

/**
 * This struct is intended to provide a place to store state/context
 * information for an instance of the API.
 */
struct RelaconApi
{
    Backend *backend;
    struct Log log;
};

struct RelaconDeviceList
{
    RelaconApi *api;
    BackendDeviceList *backendDeviceList;
};

struct RelaconDevice
{
    RelaconApi *api;
    BackendDevice *backendDevice;
    uint8_t reportBuf[REPORT_BUF_LEN];
    RelaconDeviceInfo info;
};

/**
 * Finds and returns the first device on the system that matches the provided
 * criteria
 *
 * @param[in] devList The device list to search
 * @param[in] vid The vendor ID to match, or zero to match any
 * @param[in] pid The product ID to match, or zero to match any
 * @param[in] serialNum The serial number to match, or zero to match any
 *
 * @return Returns the backend device information for the first matching device,
 *         or NULL if no matching device could be found
 */
const RelaconDeviceInfo * FindFirstMatchingDevice(
    BackendDeviceList *devList,
    uint16_t vid,
    uint16_t pid,
    const char *serialNum)
{
    const RelaconDeviceInfo *devInfo = NULL;
    const RelaconDeviceInfo *candidate;

    // Iterate over all the candidate devices in the list
    while ((candidate = BackendDeviceListGetNext(devList)) != NULL)
    {
        // Check for all of the filter criteria
        if ((vid == 0 || vid == candidate->vid) &&
            (pid == 0 || pid == candidate->pid) &&
            (serialNum == NULL || (candidate->serialNum != NULL &&
                                  strcmp(serialNum, candidate->serialNum) == 0)))
        {
            devInfo = candidate;
            break;
        }
    }

    return devInfo;
}

/**
 * Reads an HID report from the device into the device report buffer. The first
 * byte of the buffer will be the report ID
 *
 * @post On success, the device reportBuf will be populated with report data
 *
 * @param[in] dev The handle of the device to read from
 * @param[in] timeout The timeout, in milliseconds, or -1 to block indefinitely
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_DEVICE_IO the device read failed
 * @retval RELACON_STATUS_ERROR_TIMEOUT the device read timed out
 */
static RelaconStatus ReadReport(
    RelaconDevice *dev,
    int timeout)
{
    return BackendDeviceReadReport(
        dev->backendDevice,
        dev->reportBuf,
        sizeof(dev->reportBuf),
        timeout);
}

/**
 * Retrieve a report from the device and parse it as a numeric decimal value
 *
 * @param[in] dev The device to read from
 * @param[in] min The minimum expected response value
 * @param[in] max The maximum expected response value
 * @param[out] value Receives the numeric value from the response
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_DEVICE_IO the device read failed
 * @retval RELACON_STATUS_ERROR_TIMEOUT the device read timed out
 * @retval RELACON_STATUS_ERROR_BAD_RESPONSE the response contained unexpected
 *                                           data
 */
static RelaconStatus ReadNumericResponse(
    RelaconDevice *dev,
    long min,
    long max,
    long *value)
{
    struct Log *log = &dev->api->log;
    RelaconStatus status = ReadReport(dev, DEFAULT_TIMEOUT_MS);
    
    if (status == RELACON_STATUS_SUCCESS)
    {
        if (dev->reportBuf[0] != REPORT_ID_CMD)
        {
            LOG_ERROR(log, "Received unexpected report ID: %d\n", dev->reportBuf[0]);
            status = RELACON_STATUS_ERROR_BAD_RESPONSE;
        }
        else
        {
            // Attempt to parse the response as a decimal value
            char *endptr;
            long tmp = strtol((const char*)&dev->reportBuf[1], &endptr, 10);

            if (*endptr != '\0')
            {
                LOG_ERROR(log, "Failed to parse numeric value from response\n");
                status = RELACON_STATUS_ERROR_BAD_RESPONSE;
            }
            else if (tmp < min || tmp > max)
            {
                LOG_ERROR(log, "Response out of range: %ld\n", tmp);
                status = RELACON_STATUS_ERROR_BAD_RESPONSE;
            }
            else
            {
                *value = tmp;
            }
        }
    }

    return status;
}

/**
 * Writes the report from the device's reportBuf buffer to the HID device
 *
 * @pre The device reportBuf must be populated with a valid OUT report
 *
 * @param[in] dev The device instance to write to
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_DEVICE_IO an error occurred sending the output
 *                                        report to the device
 */
static RelaconStatus WriteReport(RelaconDevice *dev)
{
    return BackendDeviceWriteReport(
        dev->backendDevice,
        dev->reportBuf,
        sizeof(dev->reportBuf));
}

/**
 * Sends an HID report to the device using the command report index
 * 
 * @param[in] dev The handle of the device to operate on
 * @param[in] fmt A format string for the command to be issued
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_INTERNAL the command could not be formatted
 * @retval RELACON_STATUS_ERROR_DEVICE_IO an error occurred sending the output
 *                                        report to the device
 */
static RelaconStatus WriteFormattedCmd(RelaconDevice *dev, const char *fmt, ...)
{
    struct Log *log = &dev->api->log;

    // Populate the actual command data portion of the buffer using the
    // specified format string
    va_list vl;
    va_start(vl, fmt);
    int res = vsnprintf((char*)&dev->reportBuf[1], REPORT_DATA_LEN, fmt, vl);
    va_end(vl);

    RelaconStatus status = RELACON_STATUS_SUCCESS;

    // Check for successful formatting
    if (res < 0)
    {
        LOG_ERROR(log, "Command formatting failed\n");
        status = RELACON_STATUS_ERROR_INTERNAL;
    }
    else if (res > REPORT_DATA_LEN)
    {
        LOG_ERROR(log, "Formatting output exceeds report data bounds\n");
        status = RELACON_STATUS_ERROR_INTERNAL;
    }
    else
    {
        // Populate the report ID byte
        dev->reportBuf[0] = REPORT_ID_CMD;

        // Zero pad the remainder of the report data after the command string
        memset(&dev->reportBuf[1 + res], 0, sizeof(dev->reportBuf) - res - 1);

        // Write the report buffer out to the HID device
        status = WriteReport(dev);
    }

    return status;
}

RelaconApi* RelaconApiInit()
{
    RelaconApi *api = malloc(sizeof(RelaconApi));

    if (api == NULL)
    {
        LOG_ERROR(NULL, "Failed to allocate Relacon API context\n");
    }
    else
    {
        LogInit(&api->log);
        RelaconStatus status = BackendInit(&api->log, &api->backend);

        if (status != RELACON_STATUS_SUCCESS)
        {
            free(api);
            api = NULL;
        }
    }

    return api;
}

RelaconStatus RelaconApiExit(RelaconApi *api)
{
    if (api == NULL)
    {
        LOG_ERROR(NULL, "api argument was NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    RelaconStatus status = BackendExit(api->backend);
    free(api);

    return status;
}

RelaconDeviceList* RelaconDeviceListCreate(RelaconApi *api)
{
    struct Log *log = &api->log;
    RelaconDeviceList *devList = malloc(sizeof(RelaconDeviceList));

    if (devList == NULL)
    {
        LOG_ERROR(log, "Failed to allocate memory for device list\n");
    }
    else
    {
        RelaconStatus status = BackendDeviceListCreate(
            api->backend,
            &devList->backendDeviceList);

        if (status != RELACON_STATUS_SUCCESS)
        {
            LOG_ERROR(log, "Failed to construct device list\n");
            free(devList);
            devList = NULL;
        }
    }
    
    return devList;
}

RelaconStatus RelaconDeviceListDestroy(RelaconDeviceList *devList)
{
    RelaconStatus status =
        BackendDeviceListDestroy(devList->backendDeviceList);
    free(devList);

    return status;
}

RelaconStatus RelaconDeviceListGetNext(
    RelaconDeviceList *devList,
    RelaconDeviceInfo *devInfo)
{
    if (devList == NULL)
    {
        LOG_ERROR(NULL, "Device list argument is NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    struct Log *log = &devList->api->log;

    if (devInfo == NULL)
    {
        LOG_ERROR(log, "List entry argument is NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    RelaconStatus status = RELACON_STATUS_ERROR_NO_ENTRY;
    const RelaconDeviceInfo *devInfoInternal =
        BackendDeviceListGetNext(devList->backendDeviceList);

    if (devInfoInternal != NULL)
    {
        *devInfo = *devInfoInternal;
        status = RELACON_STATUS_SUCCESS;
    }

    return status;
}

RelaconDevice* RelaconDeviceOpen(
    RelaconApi *api,
    uint16_t vid,
    uint16_t pid,
    const char *serialNum)
{
    RelaconDevice *dev = NULL;

    RelaconDeviceList *devList = RelaconDeviceListCreate(api);

    if (devList != NULL)
    {
        struct Log *log = &api->log;

        const RelaconDeviceInfo *devInfo = FindFirstMatchingDevice(
            devList->backendDeviceList,
            vid,
            pid,
            serialNum);
        
        if (devInfo == NULL)
        {
            LOG_ERROR(log, "No matching device found\n");
        }
        else
        {
            BackendDevice *backendDevice;
            RelaconStatus status = BackendDeviceOpen(
                devInfo->handle,
                &backendDevice);

            if (status != RELACON_STATUS_SUCCESS)
            {
                LOG_ERROR(log, "Backend open failed\n");
            }
            else
            {
                dev = malloc(sizeof(*dev));

                if (dev == NULL)
                {
                    LOG_ERROR(log, "Failed to allocate memory for RelaconDevice\n");
                    BackendDeviceClose(backendDevice);
                }
                else
                {
                    dev->api = api;
                    dev->backendDevice = backendDevice;
                    dev->info = *devInfo;

                    // Copy the strings from the device info structure, as the
                    // originals will be freed when the device list is destroyed
                    if (devInfo->product != NULL)
                        dev->info.product = strdup(devInfo->product);
                    if (devInfo->manufacturer != NULL)
                        dev->info.manufacturer = strdup(devInfo->manufacturer);
                    if (devInfo->serialNum != NULL)
                        dev->info.serialNum = strdup(devInfo->serialNum);
                }
            }
        }

        // Clean up the device list
        RelaconDeviceListDestroy(devList);
    }

    return dev;
}

RelaconStatus RelaconDeviceClose(RelaconDevice *dev)
{
    // Free up the strings that were copied during open
    free(dev->info.product);
    free(dev->info.manufacturer);
    free(dev->info.serialNum);

    // Save off the handle to the BackendDevice before freeing the RelaconDevice
    BackendDevice *backendDevice = dev->backendDevice;

    free(dev);

    return BackendDeviceClose(backendDevice);
}

RelaconStatus RelaconDeviceGetInfo(
    RelaconDevice *dev,
    RelaconDeviceInfo *devInfo)
{
    if (dev == NULL)
    {
        LOG_ERROR(&dev->api->log, "Device parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    *devInfo = dev->info;

    return RELACON_STATUS_SUCCESS;
}

RelaconStatus RelaconDeviceReadInputs(
    RelaconDevice *dev,
    uint8_t *val)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    if (val == NULL)
    {
        LOG_ERROR(&dev->api->log, "Output parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    RelaconStatus status = WriteFormattedCmd(dev, "PI");

    if (status == RELACON_STATUS_SUCCESS)
    {
        long tmp;
        status = ReadNumericResponse(dev, 0, UINT8_MAX, &tmp);

        if (status == RELACON_STATUS_SUCCESS)
        {
            *val = (uint8_t)tmp;
        }
    }

    return status;
}

RelaconStatus RelaconDeviceSingleRelayWrite(
    RelaconDevice *dev,
    uint8_t relay,
    bool assert)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device argument must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    if (relay >= dev->info.numRelays)
    {
        LOG_ERROR(&dev->api->log, "Relay index was invalid\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    return WriteFormattedCmd(dev, assert ? "SK%d" : "RK%d", relay);
}

RelaconStatus RelaconDeviceSingleRelayRead(
    RelaconDevice *dev,
    uint8_t relay,
    bool *isAsserted)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device argument must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }
    
    struct Log *log = &dev->api->log;

    if (dev == NULL)
    {
        LOG_ERROR(log, "Output argument must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    if (relay >= dev->info.numRelays)
    {
        LOG_ERROR(log, "Relay index was invalid\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    RelaconStatus status = WriteFormattedCmd(dev, "RPK%d", relay);

    if (status == RELACON_STATUS_SUCCESS)
    {
        long value;
        status = ReadNumericResponse(dev, 0, 1, &value);

        if (status == RELACON_STATUS_SUCCESS)
        {
            *isAsserted = (bool)value;
        }
    }

    return status;
}

RelaconStatus RelaconDeviceRelaysWrite(
    RelaconDevice *dev,
    uint8_t val)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device argument must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    return WriteFormattedCmd(dev, "MK%03u", val);
}

RelaconStatus RelaconDeviceRelaysRead(
    RelaconDevice *dev,
    uint8_t *value)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device argument must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }
    if (dev == NULL)
    {
        LOG_ERROR(&dev->api->log, "Output argument must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    RelaconStatus status = WriteFormattedCmd(dev, "PK");

    if (status == RELACON_STATUS_SUCCESS)
    {
        long tmp;
        status = ReadNumericResponse(dev, 0, UINT8_MAX, &tmp);

        if (status == RELACON_STATUS_SUCCESS)
        {
            *value = (uint8_t)tmp;
        }
    }

    return status;
}

RelaconStatus RelaconDeviceEventCounterGet(
    RelaconDevice *dev,
    uint8_t counter,
    bool clear,
    uint16_t *value)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    struct Log *log = &dev->api->log;

    if (value == NULL)
    {
        LOG_ERROR(log, "Output parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    if (counter >= dev->info.numInputs)
    {
        LOG_ERROR(log, "Counter index out of range: %u\n", counter);
        return RELACON_STATUS_ERROR_INVALID_PARAM;
    }
    
    RelaconStatus status = WriteFormattedCmd(
        dev,
        "R%c%d",
        clear ? 'C' : 'E',
        counter);

    if (status == RELACON_STATUS_SUCCESS)
    {
        long tmp;
        status = ReadNumericResponse(dev, 0, UINT16_MAX, &tmp);

        if (status == RELACON_STATUS_SUCCESS)
        {
            *value = (uint8_t)tmp;
        }
    }

    return status;
}

RelaconStatus RelaconDeviceEventCounterDebounceSet(
    RelaconDevice *dev,
    RelaconDebounceConfig config)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    if (config < RELACON_DEBOUNCE_CONFIG_10MS ||
        config > RELACON_DEBOUNCE_CONFIG_100US)
    {
        LOG_ERROR(&dev->api->log, "Debounce config out of range: %d\n", config);
        return RELACON_STATUS_ERROR_INVALID_PARAM;
    }

    return WriteFormattedCmd(dev, "DB%d", config);
}

RelaconStatus RelaconDeviceEventCounterDebounceGet(
    RelaconDevice *dev,
    RelaconDebounceConfig *config)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    if (config == NULL)
    {
        LOG_ERROR(&dev->api->log, "Output parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    RelaconStatus status = WriteFormattedCmd(dev, "DB");

    if (status == RELACON_STATUS_SUCCESS)
    {
        long tmp;
        status = ReadNumericResponse(
            dev,
            RELACON_DEBOUNCE_CONFIG_10MS,
            RELACON_DEBOUNCE_CONFIG_100US,
            &tmp);

        if (status == RELACON_STATUS_SUCCESS)
        {
            *config = (RelaconDebounceConfig)tmp;
        }
    }

    return status;
}

RelaconStatus RelaconDeviceWatchdogSet(
    RelaconDevice *dev,
    RelaconWatchdogConfig config)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    if (config < RELACON_WATCHDOG_CONFIG_OFF ||
        config > RELACON_WATCHDOG_CONFIG_1MIN)
    {
        LOG_ERROR(&dev->api->log, "Watchdog config out of range: %d\n", config);
        return RELACON_STATUS_ERROR_INVALID_PARAM;
    }

    return WriteFormattedCmd(dev, "WD%d", config);
}

RelaconStatus RelaconDeviceWatchdogGet(
    RelaconDevice *dev,
    RelaconWatchdogConfig *config)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    if (config == NULL)
    {
        LOG_ERROR(&dev->api->log, "Output parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    RelaconStatus status = WriteFormattedCmd(dev, "WD");

    if (status == RELACON_STATUS_SUCCESS)
    {
        long tmp;
        status = ReadNumericResponse(
            dev,
            RELACON_WATCHDOG_CONFIG_OFF,
            RELACON_WATCHDOG_CONFIG_1MIN,
            &tmp);

        if (status == RELACON_STATUS_SUCCESS)
        {
            *config = (RelaconWatchdogConfig)tmp;
        }
    }

    return status;
}

RelaconStatus RelaconDeviceRawWrite(RelaconDevice *dev, const char *cmd)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    struct Log *log = &dev->api->log;

    if (cmd == NULL)
    {
        LOG_ERROR(log, "Command parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    if (strlen(cmd) > REPORT_DATA_LEN)
    {
        LOG_ERROR(log, "Provided command is too long\n");
        return RELACON_STATUS_ERROR_INVALID_PARAM;
    }

    // The first byte is always the report ID
    dev->reportBuf[0] = REPORT_ID_CMD;

    // Copy the the provided command into the report buffer, zero-padding
    // any unused bytes
    strncpy((char*)&dev->reportBuf[1], cmd, REPORT_DATA_LEN);

    // Send the command to the device
    return WriteReport(dev);
}

RelaconStatus RelaconDeviceRawRead(
    RelaconDevice *dev,
    char *buf,
    unsigned len,
    int timeout)
{
    if (dev == NULL)
    {
        LOG_ERROR(NULL, "Device parameter must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    if (buf == NULL)
    {
        LOG_ERROR(&dev->api->log, "Output buffer must not be NULL\n");
        return RELACON_STATUS_ERROR_ARGUMENT_NULL;
    }

    RelaconStatus status = ReadReport(dev, timeout);

    if (status == RELACON_STATUS_SUCCESS)
    {
        // Copy the response string to the output buffer
        strncpy(buf, (const char*)&dev->reportBuf[1], len);
    }

    return status;
}
