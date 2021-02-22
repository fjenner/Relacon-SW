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
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <getopt.h>
#include <string.h>

#define NUM_DIGITAL_INPUTS  8
#define NUM_RELAYS          8
#define TIMEOUT_DEFAULT     500

#define ARRAY_COUNT(x) (sizeof(x) / sizeof(x[0]))

/** The operation that the user is requesting (mutually exclusive) */
enum Operation
{
    OPERATION_NONE,
    OPERATION_PRINT_HELP,
    OPERATION_PRINT_VERSION,
    OPERATION_LIST_DEVICES,
    OPERATION_RW_ALL_RELAYS,
    OPERATION_RW_SINGLE_RELAY,
    OPERATION_R_DIGITAL_INPUTS,
    OPERATION_R_EVENT_COUNTER,
    OPERATION_RW_DEBOUNCE,
    OPERATION_RW_WATCHDOG,
};

/** Information provided on the command line */
struct CommandLineOptions
{
    /** The program name as extracted from argv[0] */
    const char *progName;

    /** The vendor ID of the device to open, or zero any */
    uint16_t vid;

    /** The product ID of the device to open, or zero for any */
    uint16_t pid;

    /** The serial number of the device to open, or zero for any */
    char *serialNumber;

    /** The operation selected by the command line arguments */
    enum Operation operation;

    //
    // Operation-specific parameters
    //

    /** The selected relay to for OPERATION_RW_SINGLE_RELAY */
    uint8_t relayIndex;

    /** Event counter index for OPERATION_R_EVENT_COUNTER */
    uint8_t counterIndex;

    /** Clear event counter after reading with OPERATION_R_EVENT_COUNTER? */
    bool clearOnRead;

    /** The value to write for any of the OPERATION_RW_* operations */
    char *writeValue;
};

/**
 * The getopt_long options for parsing command-line arguments
 */
static const struct option LONG_OPTIONS[] =
{
    { "list-devices",   no_argument,        NULL, 'l' },
    { "vendor-id",      required_argument,  NULL, 'v' },
    { "product-id",     required_argument,  NULL, 'p' },
    { "serial-num",     required_argument,  NULL, 's' },
    { "debounce",       no_argument,        NULL, 'd' },
    { "watchdog",       no_argument,        NULL, 'w' },
    { "individual",     required_argument,  NULL, 'i' },
    { "event-counter",  required_argument,  NULL, 'e' },
    { "clear",          no_argument,        NULL, 'c' },
    { "digital",        no_argument,        NULL, 'g' },
    { "help",           no_argument,        NULL, 'h' },
    { "version",        no_argument,        NULL, 'V' },

    // End-of-list entry
    { NULL, 0, NULL, 0 }
};

#define GETOPT_STRING "lv:p:s:dwi:e:cghV"

/**
 * Prints a usage message to the specified output stream
 *
 * @param[in] file The output stream to write the usage message to
 * @param[in] progName The name of this program as invoked from the command line
 */
static void PrintUsage(FILE *file, const char *progName)
{
    fprintf(file, "Usage: %s [-l|-d|-w|-i|-e|-g] [OPTION]... [WRITE_VALUE]\n", progName);
    fprintf(file, "\n");
    fprintf(file, "  -l, --list-devices       list available devices and exit\n");
    fprintf(file, "  -v, --vendor-id=ID       open device with the specified USB vendor ID\n");
    fprintf(file, "  -p, --product-id=ID      open device with the specified USB product ID\n");
    fprintf(file, "  -s, --serial-num=SERIAL  open device with the specified USB serial number\n");
    fprintf(file, "  -d, --debounce           read or write the debouce configuration\n");
    fprintf(file, "  -w, --watchdog           read or write the watchdog configuration\n");
    fprintf(file, "  -i, --individual=N       read or write the state for individual relay N\n");
    fprintf(file, "  -e, --event-counter=N    read the value of event counter N\n");
    fprintf(file, "  -c, --clear              clears the event counter on read\n");
    fprintf(file, "  -g, --digital            reads the state of the digital input pins\n");
    fprintf(file, "\n");
    fprintf(file, "Any combination of -v, -p, and -s can be used to filter which relay device\n");
    fprintf(file, "is operated on. If multiple devices match the filter criteria, the first\n");
    fprintf(file, "available device is used.\n");
    fprintf(file, "\n");
    fprintf(file, "Options -d, -w, -i, -e, and -g are mutually exclusive and determine which\n");
    fprintf(file, "device parameter is read or written. No positional arguments should be\n");
    fprintf(file, "specified for a read, whereas the WRITE_VALUE positional argument must be\n");
    fprintf(file, "specified to write a value. If none of these options is provided, the default\n");
    fprintf(file, "behavior is to read or write the state of the relay port.\n");
    fprintf(file, "\n");
    fprintf(file, "For the watchdog configuration writes, the acceptable write values are:\n");
    fprintf(file, "  0 - Watchdog timer is disabled\n");
    fprintf(file, "  1 - Watchdog timeout is 1 second\n");
    fprintf(file, "  2 - Watchdog timeout is 10 seconds\n");
    fprintf(file, "  3 - Watchdog timeout is 1 minute\n");
    fprintf(file, "\n");
    fprintf(file, "For the debounce configuration writes, the acceptable write values are:\n");
    fprintf(file, "  0 - Debounce window is 10ms\n");
    fprintf(file, "  1 - Debounce window is 1ms\n");
    fprintf(file, "  2 - Debounce window is 100us\n");
}

/**
 * Parses a numeric value from a string and validates that it is between the
 * specified minimum and maximum values (inclusive). On failure to parse, or if
 * the value is out of range, and appropriate error message is printed and the
 * function returns false.
 *
 * @param[in] str The string to parse
 * @param[in] min The minimum value allowed for the number
 * @param[in] max The maximum value allowed for the number
 * @param[out] out The output value to be populated
 *
 * @return Returns true if the number was successfully parsed and valid, or
 *         false on failure
 */
static bool ParseAndValidateNumber(const char *str, long min, long max, long *out)
{
    bool success = false;

    char *endptr;
    long val = strtol(str, &endptr, 0);

    if (*endptr != '\0')
        fprintf(stderr, "Failed to parse numeric value %s\n", str);
    else if (val < min || val > max)
        fprintf(stderr, "Value %ld is outside valid range [%ld, %ld]\n", val, min, max);
    else
    {
        success = true;
        *out = val;
    }

    return success;
}

/**
 * Checks that this operation does not conflict with another opertion already
 * specified. Prints an error and returns false if there is a conflict, or
 * assigns the new operation and returns true otherwise.
 *
 * @param[in,out] dst The existing operation to check and update
 * @param[in] src The new operation to use
 *
 * @return Returns false if this is a conflicting operation; true otherwise
 */
static bool ValidateAndSetOperation(enum Operation *dst, enum Operation src)
{
    bool success = (*dst == OPERATION_NONE || *dst == src);

    if (success)
        *dst = src;
    else
        fprintf(stderr, "Conflicting options specified on command line\n");

    return success;
}

static bool SimpleOperationPrintHelp(const struct CommandLineOptions *options)
{
    PrintUsage(stdout, options->progName);
    return true;
}

static bool SimpleOperationPrintVersion(const struct CommandLineOptions *options)
{
    (void)options;
    printf("Version: %s\n", RELACON_CLI_VERSION);
    return true;
}

static bool ApiOperationListDevices(
    RelaconApi *api,
    const struct CommandLineOptions *options)
{
    bool success = false;
    RelaconDeviceList* devList = RelaconDeviceListCreate(api);

    if (devList == NULL)
    {
        fprintf(stderr, "Failed to create device list\n");
    }
    else
    {
        RelaconDeviceInfo devInfo;
        while (RelaconDeviceListGetNext(devList, &devInfo) == RELACON_STATUS_SUCCESS)
        {
            // Filter by specified VID, PID, and serial number, if provided
            if ((options->vid == 0 || devInfo.vid == options->vid) &&
                (options->pid == 0 || devInfo.pid == options->pid) &&
                (options->serialNumber == NULL ||
                 (devInfo.serialNum != NULL &&
                  strcmp(options->serialNumber, devInfo.serialNum) == 0)))
            {
                printf(
                    "%04hx:%04hx: %s - %s (%s)\n",
                    devInfo.vid,
                    devInfo.pid,
                    devInfo.manufacturer ? devInfo.manufacturer : "<NO MANUFACTURER>",
                    devInfo.product ? devInfo.product : "<NO PRODUCT>",
                    devInfo.serialNum ? devInfo.serialNum : "<NO SERIAL NUMBER>");
                printf(
                    "\t%u relays, %u inputs\n",
                    devInfo.numRelays,
                    devInfo.numInputs);
            }
        }

        success = true;

        RelaconDeviceListDestroy(devList);
    }

    return success;
}

static bool DeviceOperationReadDigitalInputs(
    RelaconDevice *dev,
    const struct CommandLineOptions *options)
{
    (void)options;

    bool success = false;
    uint8_t value;

    RelaconStatus status = RelaconDeviceReadInputs(dev, &value);

    if (status != RELACON_STATUS_SUCCESS)
    {
        fprintf(stderr, "Failed to read digital inputs (status=%d)\n", status);
    }
    else
    {
        printf("0x%02" PRIx8 "\n", value);
        success = true;
    }

    return success;
}

static bool DeviceOperationReadEventCounter(
    RelaconDevice *dev,
    const struct CommandLineOptions *options)
{
    bool success = false;
    uint16_t count;

    RelaconStatus status = RelaconDeviceEventCounterGet(
        dev,
        options->counterIndex,
        options->clearOnRead,
        &count);
    
    if (status != RELACON_STATUS_SUCCESS)
    {
        fprintf(stderr, "Failed to read event counter (status=%d)\n", status);
    }
    else
    {
        printf("0x%04" PRIx16 "\n", count);
        success = true;
    }

    return success;
}

static bool DeviceOperationReadWriteRelays(
    RelaconDevice *dev,
    const struct CommandLineOptions *options)
{
    bool success = false;

    if (options->writeValue == NULL)
    {
        // If there was no write value, then just read the current relays state
        uint8_t value;
        RelaconStatus status = RelaconDeviceRelaysRead(dev, &value);

        if (status != RELACON_STATUS_SUCCESS)
        {
            fprintf(stderr, "Failed to read relays (status=%d)\n", status);
        }
        else
        {
            printf("0x%02" PRIx8 "\n", value);
            success = true;
        }
    }
    else
    {
        // If there was a value specified, parse it and set the new relay state
        long value;
        success = ParseAndValidateNumber(
                options->writeValue,
                0,
                UINT8_MAX,
                &value);

        if (success)
        {
            RelaconStatus status = RelaconDeviceRelaysWrite(dev, (uint8_t)value);

            if (status != RELACON_STATUS_SUCCESS)
            {
                fprintf(stderr, "Failed to write relays (status=%d)\n", status);
            }
            else
            {
                success = true;
            }
        }
    }

    return success;
}

static bool DeviceOperationReadWriteSingleRelay(
    RelaconDevice *dev,
    const struct CommandLineOptions *options)
{
    bool success = false;

    if (options->writeValue == NULL)
    {
        // If there was no write value, then just read the current relay state
        bool isClosed;
        RelaconStatus status = RelaconDeviceSingleRelayRead(
            dev,
            options->relayIndex,
            &isClosed);

        if (status != RELACON_STATUS_SUCCESS)
        {
            fprintf(stderr, "Failed to read relay (status=%d)\n", status);
        }
        else
        {
            printf("%d\n", (int)isClosed);
            success = true;
        }
    }
    else
    {
        // If there was a value specified, parse it and set the new relay state
        long value;
        success = ParseAndValidateNumber(
            options->writeValue,
            (long)false,
            (long)true,
            &value);
        
        if (success)
        {
            RelaconStatus status = RelaconDeviceSingleRelayWrite(
                dev,
                options->relayIndex,
                (bool)value);

            if (status != RELACON_STATUS_SUCCESS)
            {
                fprintf(stderr, "Failed to write relay (status=%d)\n", status);
            }
            else
            {
                success = true;
            }
        }
    }

    return success;
}

static bool DeviceOperationReadWriteDebounce(
    RelaconDevice *dev,
    const struct CommandLineOptions *options)
{
    bool success = false;

    if (options->writeValue == NULL)
    {
        // If there was no write value, then just read the current relays state
        RelaconDebounceConfig value;
        RelaconStatus status = RelaconDeviceEventCounterDebounceGet(dev, &value);

        if (status != RELACON_STATUS_SUCCESS)
        {
            fprintf(stderr, "Failed to read debounce value (status=%d)\n", status);
        }
        else
        {
            const char * configString;

            success = true;
            switch (value)
            {
                case RELACON_DEBOUNCE_CONFIG_100US:
                    configString = "100us";
                    break;
                case RELACON_DEBOUNCE_CONFIG_1MS:
                    configString = "1ms";
                    break;
                case RELACON_DEBOUNCE_CONFIG_10MS:
                    configString = "10ms";
                    break;
                default:
                    fprintf(stderr, "Received unexpected debounce setting\n");
                    success = false;
                    break;
            }

            if (success)
            {
                printf("Debounce setting: %s\n", configString);
            }
        }
    }
    else
    {
        // If there was a value specified, parse it and set the new relay state
        long value;
        success = ParseAndValidateNumber(
            options->writeValue,
            RELACON_DEBOUNCE_CONFIG_10MS,
            RELACON_DEBOUNCE_CONFIG_100US,
            &value);
        
        if (success)
        {
            RelaconStatus status = RelaconDeviceEventCounterDebounceSet(
                dev,
                (RelaconDebounceConfig)value);

            if (status != RELACON_STATUS_SUCCESS)
            {
                fprintf(stderr, "Failed to set debounce (status=%d)\n", status);
            }
            else
            {
                success = true;
            }
        }
    }

    return success;
}

static bool DeviceOperationReadWriteWatchdog(
    RelaconDevice *dev,
    const struct CommandLineOptions *options)
{
    bool success = false;

    if (options->writeValue == NULL)
    {
        // If there was no write value, then just read the current relays state
        RelaconWatchdogConfig value;
        RelaconStatus status = RelaconDeviceWatchdogGet(dev, &value);

        if (status != RELACON_STATUS_SUCCESS)
        {
            fprintf(stderr, "Failed to read watchdog value (status=%d)\n", status);
        }
        else
        {
            const char * configString;

            success = true;
            switch (value)
            {
                case RELACON_WATCHDOG_CONFIG_OFF:
                    configString = "OFF";
                    break;
                case RELACON_WATCHDOG_CONFIG_1SEC:
                    configString = "1s";
                    break;
                case RELACON_WATCHDOG_CONFIG_10SEC:
                    configString = "10s";
                    break;
                case RELACON_WATCHDOG_CONFIG_1MIN:
                    configString = "1m";
                    break;
                default:
                    fprintf(stderr, "Received unexpected watchdog setting\n");
                    success = false;
                    break;
            }

            if (success)
            {
                printf("Watchdog setting: %s\n", configString);
            }
        }
    }
    else
    {
        // If there was a value specified, parse it and set the new relay state
        long value;
        success = ParseAndValidateNumber(
            options->writeValue,
            RELACON_WATCHDOG_CONFIG_OFF,
            RELACON_WATCHDOG_CONFIG_1MIN,
            &value);
        
        if (success)
        {
            RelaconStatus status = RelaconDeviceWatchdogSet(
                dev,
                (RelaconWatchdogConfig)value);

            if (status != RELACON_STATUS_SUCCESS)
            {
                fprintf(stderr, "Failed to set watchdog (status=%d)\n", status);
            }
            else
            {
                success = true;
            }
        }
    }

    return success;
}

/** Function type for handling an operation requiring no context */
typedef bool (*SimpleOperationHandler)(const struct CommandLineOptions*);

/** Function type for handling an operation requiring only a RelaconApi */
typedef bool (*ApiOperationHandler)(RelaconApi *api, const struct CommandLineOptions*);

/** Function type for handling an operation requiring a RelaconDevice */
typedef bool (*DeviceOperationHandler)(RelaconDevice*, const struct CommandLineOptions*);

static const SimpleOperationHandler SIMPLE_OPERATION_HANDLERS[] =
{
    [OPERATION_PRINT_HELP] = SimpleOperationPrintHelp,
    [OPERATION_PRINT_VERSION] = SimpleOperationPrintVersion,
};

static const ApiOperationHandler API_OPERATION_HANDLERS[] =
{
    [OPERATION_LIST_DEVICES] = ApiOperationListDevices,
};

static const DeviceOperationHandler DEVICE_OPERATION_HANDLERS[] =
{
    [OPERATION_R_DIGITAL_INPUTS] = DeviceOperationReadDigitalInputs,
    [OPERATION_R_EVENT_COUNTER] = DeviceOperationReadEventCounter,
    [OPERATION_RW_SINGLE_RELAY] = DeviceOperationReadWriteSingleRelay,
    [OPERATION_RW_ALL_RELAYS] = DeviceOperationReadWriteRelays,
    [OPERATION_RW_DEBOUNCE] = DeviceOperationReadWriteDebounce,
    [OPERATION_RW_WATCHDOG] = DeviceOperationReadWriteWatchdog,
};

static bool RunOperation(struct CommandLineOptions *options)
{
    bool success = false;

    // Check if this is an operation not requiring the Relacon API
    if (options->operation < ARRAY_COUNT(SIMPLE_OPERATION_HANDLERS) &&
        SIMPLE_OPERATION_HANDLERS[options->operation] != NULL)
    {
        success = SIMPLE_OPERATION_HANDLERS[options->operation](options);
    }
    else
    {
        // This operation must require at least a Relacon API instance
        RelaconApi *api = RelaconApiInit();
        if (api == NULL)
        {
            fprintf(stderr, "Failed to initialize Relacon API\n");
        }
        else
        {
            // Check if this is an operation requiring only a RelaconApi instance
            if (options->operation < ARRAY_COUNT(API_OPERATION_HANDLERS) &&
                API_OPERATION_HANDLERS[options->operation] != NULL)
            {
                success = API_OPERATION_HANDLERS[options->operation](api, options);
            }
            else
            {
                // This operation must require a RelaconDevice instance
                RelaconDevice *dev = RelaconDeviceOpen(
                    api,
                    options->vid,
                    options->pid,
                    options->serialNumber);

                if (dev == NULL)
                {
                    fprintf(stderr, "Failed to open Relacon device\n");
                }
                else
                {
                    // Check if this is an operation requiring only a RelaconDevice instance
                    if (options->operation < ARRAY_COUNT(DEVICE_OPERATION_HANDLERS) &&
                        DEVICE_OPERATION_HANDLERS[options->operation] != NULL)
                    {
                        success = DEVICE_OPERATION_HANDLERS[options->operation](dev, options);
                    }
                    else
                    {
                        fprintf(stderr, "No handler found for operation\n");
                    }

                    // Always close the device if we opened it
                    RelaconDeviceClose(dev);
                }
            }

            // Always clean up the API context if we initialized it
            RelaconApiExit(api);
        }
    }

    return success;
}

/**
 * Parses the command line arguments array into a CommandLineOptions structure
 *
 * @param[in] argc The number of command line arguments
 * @param[in] argv The array of command line arguments
 * @param[out] selectedOptions The output structure to be populated
 *
 * @return Returns true on success or false on failure
 */
static bool ParseCommandLine(
    int argc,
    char *argv[],
    struct CommandLineOptions *selectedOptions)
{
    bool success = true;
    long longValue;

    // Set up default values which might not otherwise be populated below
    selectedOptions->vid = 0;
    selectedOptions->pid = 0;
    selectedOptions->serialNumber = NULL;
    selectedOptions->operation = OPERATION_NONE;
    selectedOptions->counterIndex = 0;
    selectedOptions->clearOnRead = false;
    selectedOptions->relayIndex = 0;
    selectedOptions->writeValue = NULL;

    int c;
    while (success &&
        (c = getopt_long(argc, argv, GETOPT_STRING, LONG_OPTIONS, NULL)) != -1)
    {
        switch (c)
        {
            case 'l':
                success = ValidateAndSetOperation(
                    &selectedOptions->operation,
                    OPERATION_LIST_DEVICES);
                break;

            case 'v':
                success = ParseAndValidateNumber(
                    optarg,
                    0,
                    UINT16_MAX,
                    &longValue);
                
                if (success)
                    selectedOptions->vid = (uint16_t)longValue;
                break;

            case 'p':
                success = ParseAndValidateNumber(
                    optarg,
                    0,
                    UINT16_MAX,
                    &longValue);
                
                if (success)
                    selectedOptions->pid = (uint16_t)longValue;
                break;
            
            case 's':
                selectedOptions->serialNumber = optarg;
                break;
            
            case 'd':
                success = ValidateAndSetOperation(
                    &selectedOptions->operation,
                    OPERATION_RW_DEBOUNCE);
                break;
            
            case 'w':
                success = ValidateAndSetOperation(
                    &selectedOptions->operation,
                    OPERATION_RW_WATCHDOG);
                break;
            
            case 'i':
                success = ValidateAndSetOperation(
                    &selectedOptions->operation,
                    OPERATION_RW_SINGLE_RELAY);
                
                if (success)
                {
                    success = ParseAndValidateNumber(
                        optarg,
                        0,
                        NUM_RELAYS - 1,
                        &longValue);

                    if (success)
                        selectedOptions->relayIndex = (uint8_t)longValue;
                }
                break;
            
            case 'e':
                success = ValidateAndSetOperation(
                    &selectedOptions->operation,
                    OPERATION_R_EVENT_COUNTER);
                
                if (success)
                {
                    success = ParseAndValidateNumber(
                        optarg,
                        0,
                        NUM_DIGITAL_INPUTS - 1,
                        &longValue);

                    if (success)
                        selectedOptions->counterIndex = (uint8_t)longValue;
                }
                break;
            
            case 'c':
                selectedOptions->clearOnRead = true;
                break;
            
            case 'g':
                success = ValidateAndSetOperation(
                    &selectedOptions->operation,
                    OPERATION_R_DIGITAL_INPUTS);
                break;
            
            case 'h':
                
                success = ValidateAndSetOperation(
                    &selectedOptions->operation,
                    OPERATION_PRINT_HELP);
                break;

            case 'V':
                success = ValidateAndSetOperation(
                    &selectedOptions->operation,
                    OPERATION_PRINT_VERSION);
                break;

            default:
                success = false;
                break;
        }
    };

    // Interpret the first command line argument as the program name
    selectedOptions->progName = argv[0];

    // If no operation was specified, default to relay read/writes
    if (selectedOptions->operation == OPERATION_NONE)
    {
        selectedOptions->operation = OPERATION_RW_ALL_RELAYS;
    }

    if (argc - optind == 1)
    {
        // If there's one positional argument, assume it's the value to write
        selectedOptions->writeValue = argv[optind];
    }
    else if (argc - optind > 1)
    {
        // If there are additional extra arguments, error out
        fprintf(stderr, "Encountered extraneous arguments\n");
        success = false;
    }

    return success;
}

int main(int argc, char *argv[])
{
    struct CommandLineOptions selectedOptions;

    bool success = ParseCommandLine(argc, argv, &selectedOptions);

    if (!success)
    {
        PrintUsage(stderr, argv[0]);
    }
    else
    {
        success = RunOperation(&selectedOptions);
    }

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
