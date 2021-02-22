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

#ifndef RELACON_H
#define RELACON_H

#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

enum RelaconDebounceConfig
{
    RELACON_DEBOUNCE_CONFIG_10MS = 0,
    RELACON_DEBOUNCE_CONFIG_1MS = 1,
    RELACON_DEBOUNCE_CONFIG_100US = 2,
};

enum RelaconInputPort
{
    RELACON_INPUT_PORT_A,
    RELACON_INPUT_PORT_B
};

enum RelaconWatchdogConfig
{
    RELACON_WATCHDOG_CONFIG_OFF = 0,
    RELACON_WATCHDOG_CONFIG_1SEC = 1,
    RELACON_WATCHDOG_CONFIG_10SEC = 2,
    RELACON_WATCHDOG_CONFIG_1MIN = 3,
};

enum RelaconStatus
{
    RELACON_STATUS_SUCCESS = 0,
    RELACON_STATUS_ERROR_ARGUMENT_NULL,
    RELACON_STATUS_ERROR_INVALID_PARAM,
    RELACON_STATUS_ERROR_OUT_OF_MEMORY,
    RELACON_STATUS_ERROR_TIMEOUT,
    RELACON_STATUS_ERROR_BAD_RESPONSE,
    RELACON_STATUS_ERROR_DEVICE_IO,
    RELACON_STATUS_ERROR_INTERNAL,
    RELACON_STATUS_ERROR_NO_ENTRY
};


/**
 * Holds information about a potential Relacon (or compatible) device.
 */
struct RelaconDeviceInfo
{
    /** The vendor ID */
    uint16_t vid;

    /** The product ID */
    uint16_t pid;

    /** The serial number */
    char *serialNum;

    /** The manufacturer string */
    char *manufacturer;

    /** The product string */
    char *product;

    /** Opaque handle for opening the device */
    void *handle;

    /** The number of relays on the device */
    unsigned numRelays;

    /** The number of digital inputs on the device */
    unsigned numInputs;
};

struct RelaconApi;
struct RelaconDeviceList;
struct RelaconDevice;

typedef struct RelaconApi RelaconApi;
typedef struct RelaconDeviceList RelaconDeviceList;
typedef struct RelaconDevice RelaconDevice;
typedef struct RelaconDeviceInfo RelaconDeviceInfo;

typedef enum RelaconStatus RelaconStatus;
typedef enum RelaconInputPort RelaconStatusInputPort;
typedef enum RelaconDebounceConfig RelaconDebounceConfig;
typedef enum RelaconWatchdogConfig RelaconWatchdogConfig;

/**
 * Initializes the Relacon API. This returns a handle that must be used
 * for other calls in this API.
 * 
 * @return Returns a handle on success, or NULL on failure
 */
RelaconApi* RelaconApiInit();

/**
 * Clean up the library
 *
 * @param[in] handle The handle to the API instance
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p api argument was null
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal failure occured during
 *                                       cleanup
 */
RelaconStatus RelaconApiExit(RelaconApi *api);

/**
 * Creates a list of all detected HID devices with the specified vendor ID
 * (VID) and/or product (ID).
 *
 * @param[in] api The API instance in use
 *
 * @return Returns a handle to a list of devices on success, or NULL
 *         on failure
 */
RelaconDeviceList* RelaconDeviceListCreate(RelaconApi *api);

/**
 * Frees the resources associated with a device list
 *
 * @param[in] devList The device list to be destroyed
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal failure occured during
 *                                       cleanup
 */
RelaconStatus RelaconDeviceListDestroy(RelaconDeviceList *devList);

/**
 * Returns the next entry in the device list, or NULL if at the end of the
 * list. There is no rewind functionality; to traverse the list again, it must
 * be destroyed and recreated.
 *
 * @param[in] devList The device list being traversed
 * @param[out] devInfo Receives a the device information
 * 
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p devList or @p listEntry
 *                                            arguments were NULL
 * @retval RELACON_STATUS_ERROR_NO_ENTRY there are no more list entries
 */
RelaconStatus RelaconDeviceListGetNext(
    RelaconDeviceList *devList,
    RelaconDeviceInfo *devInfo);

/**
 * Opens the selected device given the vendor ID, product ID, and, optionally,
 * the serial number of the device instance. If the serial number is provided
 * as NULL, then the first matching device instance is opened. This must be
 * called before other device operations may be invoked.
 *
 * @param[in,out] relacon The library instance handle to use
 * @param[in] vid The USB vendor ID of the device to open
 * @param[in] pid The USB product ID of the device to open
 * @param[in] serialNum The USB serial number string of the device instance, or
 *                      NULL to use the first instance detected
 * 
 * @return Returns a handle to the device on success, or NULL on failure
 */
RelaconDevice* RelaconDeviceOpen(
    RelaconApi *relacon,
    uint16_t vid,
    uint16_t pid,
    const char *serialNum);

/**
 * Closes the device with the specified handle
 *
 * @param dev The handle of the device to close
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_INTERNAL a failure occurred
 */
RelaconStatus RelaconDeviceClose(RelaconDevice *dev);

/**
 * Populates the provided structure with the information for the open device.
 * The lifetime of the string fields pointed to by the structure is bound to
 * the lifetime of the RelaconDevice and therefore are no longer valid once
 * the device is closed.
 *
 * @param[in] dev The device whose information to retrieve
 * @param[out] devInfo Receives the information about the open device
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev argument was null
 */
RelaconStatus RelaconDeviceGetInfo(
    RelaconDevice *dev,
    RelaconDeviceInfo *devInfo);

/**
 * Reads the concatenated values of the inputs from "PORT A" (the four LSBs)
 * and "PORT B" (the four MSBs)
 *
 * @param[in] dev The handle of the device to read from
 * @param[out] val The value of the input ports
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev or @p val arguments
 *                                            were null
 * @retval RELACON_STATUS_ERROR_DEVICE_IO the device read failed
 * @retval RELACON_STATUS_ERROR_TIMEOUT the device read timed out
 * @retval RELACON_STATUS_ERROR_BAD_RESPONSE the response contained unexpected
 *                                           data
 */
RelaconStatus RelaconDeviceReadInputs(
    RelaconDevice *dev,
    uint8_t *val);

/**
 * Opens or closes the specified relay
 *
 * @param[in] dev The handle of the device instance to write to
 * @param[in] relay The index of the relay to affect
 * @param[in] assert Specify true to close the relay or false to open the relay
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev argument was null
 * @retval RELACON_STATUS_ERROR_INVALID_PARAM the relay index was invalid
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal error occurred
 * @retval RELACON_STATUS_ERROR_DEVICE_IO an error occurred sending the output
 *                                        report to the device
 */
RelaconStatus RelaconDeviceSingleRelayWrite(
    RelaconDevice *dev,
    uint8_t relay,
    bool assert);

/**
 * Retrieves the state of a single relay
 *
 * @param[in] dev The handle of the device instance to read from
 * @param[in] relay The index of the relay to query
 * @param[out] isAsserted Receives a value of true if the relay is closed or
 *                        false if the relay is open
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev or @p isAsserted
 *                                            arguments were null
 * @retval RELACON_STATUS_ERROR_INVALID_PARAM the relay index was invalid
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal error occurred
 * @retval RELACON_STATUS_ERROR_DEVICE_IO an error occurred sending the output
 *                                        report to the device
 */
RelaconStatus RelaconDeviceSingleRelayRead(
    RelaconDevice *dev,
    uint8_t relay,
    bool *isAsserted);

/**
 * Writes the specified value to the relay port ("PORT K")
 *
 * @param[in] dev The handle of the device instance to write to
 * @param[in] val The value to write to the relay port
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev argument was null
 * @retval RELACON_STATUS_ERROR_INTERNAL an internal error occurred
 * @retval RELACON_STATUS_ERROR_DEVICE_IO an error occurred sending the output
 *                                        report to the device
 */
RelaconStatus RelaconDeviceRelaysWrite(
    RelaconDevice *dev,
    uint8_t val);

/**
 * Reads the current state of the relay port ("PORT K")
 *
 * @param[in] dev The handle of the device instance to read from
 * @param[out] val The current state of the relay port
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev or @p value arguments
 *                                            were null
 * @retval RELACON_STATUS_ERROR_DEVICE_IO the device read failed
 * @retval RELACON_STATUS_ERROR_TIMEOUT the device read timed out
 * @retval RELACON_STATUS_ERROR_BAD_RESPONSE the response contained unexpected
 *                                           data
 */
RelaconStatus RelaconDeviceRelaysRead(
    RelaconDevice *dev,
    uint8_t *value);

/**
 * Gets the current event count of the specified input counter
 * 
 * @param[in] dev The handle of the device whose event counter to read
 * @param[in] counter The identifier of the counter to read. 0 corresponds to
 *                    "PORT A" bit 0 and 7 corresponds to "PORT B" bit 3
 * @param[in] clear If true, clears the counter upon reading
 * @param[out] value The current value of the event counter
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev or @p val arguments
 *                                            were null
 * @retval RELACON_STATUS_ERROR_INVALID_PARAM the event counter index was out
 *                                            of range
 * @retval RELACON_STATUS_ERROR_DEVICE_IO the device read failed
 * @retval RELACON_STATUS_ERROR_TIMEOUT the device read timed out
 * @retval RELACON_STATUS_ERR_RBAD_RESPONSE the response data could not be
 *                                          parsed as expected
 */
RelaconStatus RelaconDeviceEventCounterGet(
    RelaconDevice *dev,
    uint8_t counter,
    bool clear,
    uint16_t *value);

/**
 * Sets the debounce configuration for the event counters
 *
 * @param[in] dev The handle of the device whose debounce configuration to set
 * @param[in] config The new debounce configuration
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev argument was null
 * @retval RELACON_STATUS_ERROR_INVALID_PARAM the @p config value was invalid
 * @retval RELACON_STATUS_ERROR_INTERNAL the command could not be formatted
 * @retval RELACON_STATUS_ERROR_DEVICE_IO an error occurred sending the output
 *                                        report to the device
 */
RelaconStatus RelaconDeviceEventCounterDebounceSet(
    RelaconDevice *dev,
    RelaconDebounceConfig config);

/**
 * Gets the debounce configuration for the event counters
 *
 * @param[in] dev The handle of the device whose debounce configuration to get
 * @param[out] config The current debounce configuration
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev ir @p config arguments
 *                                            were null
 * @retval RELACON_STATUS_ERROR_DEVICE_IO the device read failed
 * @retval RELACON_STATUS_ERROR_TIMEOUT the device read timed out
 * @retval RELACON_STATUS_ERROR_BAD_RESPONSE the response contained unexpected
 *                                           data
 */
RelaconStatus RelaconDeviceEventCounterDebounceGet(
    RelaconDevice *dev,
    RelaconDebounceConfig *config);

/**
 * Sets the watchdog configuration for the device. The device will clear (open)
 * all relays and disable the watchdog if the device does not receive at least
 * one host command before the next watchdog expiration. The watchdog timer is
 * reset each time the host sends a command to the device.
 *
 * @param[in] dev The handle of the device whose watchdog configuration to set
 * @param[in] config The new watchdog configuration
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev argument was null
 * @retval RELACON_STATUS_ERROR_INVALID_PARAM the @p config value was invalid
 * @retval RELACON_STATUS_ERROR_INTERNAL the command could not be formatted
 * @retval RELACON_STATUS_ERROR_DEVICE_IO an error occurred sending the output
 *                                        report to the device
 */
RelaconStatus RelaconDeviceWatchdogSet(
    RelaconDevice *dev,
    RelaconWatchdogConfig config);

/**
 * Gets the watchdog configuration for the device.
 *
 * @param[in] dev The handle of the device whose watchdog configuration to set
 * @param[out] config The current watchdog configuration
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev ir @p config arguments
 *                                            were null
 * @retval RELACON_STATUS_ERROR_DEVICE_IO the device read failed
 * @retval RELACON_STATUS_ERROR_TIMEOUT the device read timed out
 * @retval RELACON_STATUS_ERROR_BAD_RESPONSE the response contained unexpected
 *                                           data
 */
RelaconStatus RelaconDeviceWatchdogGet(
    RelaconDevice *dev,
    RelaconWatchdogConfig *config);

/**
 * Write a raw ASCII command to the device. This bypasses the API abstractions
 * and allows the use of the underlying ASCII command protocol to write a
 * command to the device. Only use this if you know what you're doing...
 *
 * @param[in] dev The handle of the device to write to
 * @param[in] cmd The ASCII command to write to the device
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_INVALID_PARAM the @p cmd string was too long
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev or @p cmd argument was
 *                                            NULL
 * @retval RELACON_STATUS_ERROR_DEVICE_IO an error occurred sending the output
 *                                        report to the device
 */
RelaconStatus RelaconDeviceRawWrite(RelaconDevice *dev, const char *cmd);

/**
 * Read a raw ASCII response from the device. A response is only issued in
 * response to a command, and only certain commands solicit a response. This
 * function will block indefinitely if no response is received. Also, the
 * output buffer will be NULL terminated only if it is large enough to fit the
 * entire response string plus the NULL terminatoe. Only use this if you know
 * what you're doing...
 *
 * @param[in] dev The handle of the device to read from
 * @param[out] buf The buffer to populate with the ASCII response
 * @param[in] len The length of the output buffer
 * @param[in] timeout The timeout, in milliseconds, or -1 to block indefinitely
 *
 * @retval RELACON_STATUS_SUCCESS the operation completed successfully
 * @retval RELACON_STATUS_ERROR_ARGUMENT_NULL the @p dev or @p buf arguments
 *                                            were NULL
 * @retval RELACON_STATUS_ERROR_DEVICE_IO the device read failed
 * @retval RELACON_STATUS_ERROR_TIMEOUT the device read timed out
 */
RelaconStatus RelaconDeviceRawRead(
    RelaconDevice *dev,
    char *buf,
    unsigned len,
    int timeout);

#ifdef __cplusplus
}
#endif

#endif
