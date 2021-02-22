# Relacon Relay Controller Software

This repository contains the host (PC) side software for interfacing with a [Relacon relay controller](https://github.com/fjenner/Relacon-HW). This software is also compatible with OnTrak relay controllers.

For the Relacon relay controller firmware, please see the [Relacon-FW](https://github.com/fjenner/Relacon-FW) repository, instead.

## License

This software is licensed under the 3-clause BSD license. Please see [LICENSE](LICENSE) for additional information.

## Building the Software

### Prerequisites

This software has been tested to build on Windows under MSYS2/MinGW64 and on Linux using Ubuntu 20.04. In MSYS2, it is expected that you're building from within the "MSYS2 MinGW 64-bit shell" and that you have the `mingw-w64-x86_64-cmake` and `mingw-w64-x86_64-hidapi` packages installed. On Ubuntu, it is assumed that you have the `cmake` and `libusb-1.0-0-dev` packages installed.

### Fetching the Code
The build process for both is very similar. First, check out the source code and enter the directory where the source was deposited:

```console
$ git clone https://github.com/fjenner/Relacon-SW.git
$ cd Relacon-SW
```

### Building the Software
As this project uses CMake for building, we will follow the convention of creating a directory called `build` within the project root where all build outputs and artifacts will go, keeping the rest of the source tree clean:

```console
$ mkdir build
$ cd build
```

Here is where things differ slightly when building on Windows versus Linux. By default, CMake uses the Makefile "generator" on Linux, so there is no need to explicitly specify a generator to use. Thus, under Linux, build simply by running the following commands:

```console
$ cmake ..
$ make
```

However, on Windows, CMake uses the Visual Studio "generator" by default. Thus, when building under Windows, we need to explicitly specify to use the MSYS Makefile "generator" in order to set up an MSYS2/MinGW64-friendly build environment that this software is intended to be built with. Thus, to build under MSYS2 in Windows, the commands must be modified slightly:

```console
$ cmake -G "MSYS Makefiles" ..
$ make
```

## Installing the Software

Installing the software is simply a matter of invoking the `install` make target from the build directory:

```console
$ make install
```

On both Linux and MSYS2, this will install the application into the /bin directory by default (though this can be configured by overriding the `CMAKE_INSTALL_PREFIX` and/or `CMAKE_INSTALL_BINDIR` CMake variables). On Linux, this will additionally install a set of udev rules for compatible USB devices.

On Windows, "installing" the software in this manner is fine if you're planning on running it from within the MSYS environment. More likely, though, you'll want to copy the executable elsewhere. However, attempting to run the executable outside of the MSYS environment will result in a runtime error due to the inability to find the HIDAPI library. The solution is simply to copy the HIDAPI .dll (likely located at `/mingw64/bin/libhidapi-0.dll`) to the same directory as the executable.

## Running the Software

The software is fairly straightforward and is described fairly succinctly by the help text that is printed when running with the `--help` option:

```console
$ relacon-cli --help
Usage: relacon-cli [-l|-d|-w|-i|-e|-g] [OPTION]... [WRITE_VALUE]

  -l, --list-devices       list available devices and exit
  -v, --vendor-id=ID       open device with the specified USB vendor ID
  -p, --product-id=ID      open device with the specified USB product ID
  -s, --serial-num=SERIAL  open device with the specified USB serial number
  -d, --debounce           read or write the debouce configuration
  -w, --watchdog           read or write the watchdog configuration
  -i, --individual=N       read or write the state for individual relay N
  -e, --event-counter=N    read the value of event counter N
  -c, --clear              clears the event counter on read
  -g, --digital            reads the state of the digital input pins

Any combination of -v, -p, and -s can be used to filter which relay device
is operated on. If multiple devices match the filter criteria, the first
available device is used.

Options -d, -w, -i, -e, and -g are mutually exclusive and determine which
device parameter is read or written. No positional arguments should be
specified for a read, whereas the WRITE_VALUE positional argument must be
specified to write a value. If none of these options is provided, the default
behavior is to read or write the state of the relay port.

For the watchdog configuration writes, the acceptable write values are:
  0 - Watchdog timer is disabled
  1 - Watchdog timeout is 1 second
  2 - Watchdog timeout is 10 seconds
  3 - Watchdog timeout is 1 minute

For the debounce configuration writes, the acceptable write values are:
  0 - Debounce window is 10ms
  1 - Debounce window is 1ms
  2 - Debounce window is 100us
```

The gist of it is that you run the program without any positional arguments to read a value the device, or run the program with a single positional argument specifying a value to write to the device. By default, the value read or written is the state of the relay port. However, the options `-d`, `-w`, `-i`, `-e`, and `-g` specify different device parameters that can be read or written, as described in the help text.

If multiple compatible devices are present on the system, then the options `-l`, `-v`, `-p`, and `-s` may be useful. The `-l` option causes the program to simply list the available devices and exit. The `-v`, `-p`, and `-s` options allow for the selection of a particular device by narrowing down the USB vendor ID, USB product ID, and/or USB serial number, respectively, of the device to open.

## Technical Quirks

There are a few quirks that make for some interesting (read, "inconvenient") technical decisions. These quirks and their implications are discussed in detail in the sections below, but the end result is that, even though both APIs are cross-platform, we still need to have some OS-dependent condtionals and also need to use HIDAPI on Windows and Libusb on Linux to get the desired functionality.

### Windows HID Collections

There is actually a fundamental difference between HID device handling on Windows vs. Linux that requires additional attention in software. For HID devices with multiple report collections, Windows will present each collection as a separate HID device. In contrast, Linux presents a single HID device.

In the case of the Relacon relay controller (to be consistent with the ADU218 that it is designed to mimic), there are 3 HID report collections. Only one such collection is actually used on this device (the command/response report collection), whereas other collections (such as the streaming report collection or the RS232 converter report collection) are vestiges of other products in the ADU family that are not actually implemented.

Unfortunately, the HIDAPI does not abstract the differences in how these collections are enumerated between Windows and Linux. This means that in Windows, HIDAPI will report 3 matching devices for any given relay controller instance whereas Linux will report only one. As such, when listing/selecting devices in Windows, this software must pay attention to the collection usage values for each device and filter out any entries that don't match the command/response collection.

### ADU218 Firmware Bug for String Descriptors

Another unexpected challenge working with the ADU218 is that its firmware does not handle string descriptor requests very robustly, resulting in empty strings in the HIDAPI `hid_device_info` struct. The ADU218 firmware appears to treat the `wLength` value in the GetDescriptor request as a `uint8_t`, rather than a `uint16_t`. This means that when the host requests more than 255 bytes for the descriptor, the ADU218 will only return length % 256 bytes. Unfortunately, as of the time of this writing, HIDAPI requests 512 bytes when using the `libusb` backend, and 1024 bytes when using the Windows HID backend. The result in both cases is that the device sees a request for 0 bytes (well, actually the underlying host software tacks on an extra two bytes to `wLength` to account for the string descriptor header), so the firmware returns only the 2-byte string descriptor header and no string data. This translates to an empty string.

The `hidraw` backend uses the strings parsed from `sysfs` entries, which does not appear to suffer from this issue. However, the `hidraw` backend has other issues with this device as described in the next section.

The `HIDAPI` _does_ have some functions (`hid_get_*_string`) that allow you to explicitly query for the string descriptors. While this seems like a reasonable workaround, it has two major caveats:
1. In order to request the string descriptors for a device with this function, the device must first be opened. This means that when the client wants to get a list of devices and their information, each device must be opened temporarily and queried. Attempting to open devices may fail if they're in use elsewhere or (on Linux) if there are insufficient permissions.
2. On the `libusb` HIDAPI backend, an intermediate buffer of 512 bytes is still used even if a smaller `maxlen` parameter is passed to the `hid_get_*_string` functions. Thus, this backend still suffers from the empty string issue.

### OnTrak ADU Compatibility with `hidraw` Backend on Linux

On Linux, the OnTrak ADU products (specifically, those using the `0x0a07` USB VID; the Relacon device does not suffer from this issue when using the [default Relacon VID/PID](https://pid.codes/1209/FA70/)) actually have a dedicated kernel driver called `adutux` that will be associated with OnTrak ADU devices by default. Because the Relacon software relies on HIDAPI for communication with the relay controller, the device must be detached from the `adutux` kernel driver and attached to one of the drivers recognized by the HIDAPI.

The HIDAPI library can use one of two backends: `libusb` or `hidraw`. If this software built with the `libusb` backend, then the device is automatically detached from the `adutux` driver and can be used as expected. On the other hand, if this software is built with the `hidraw` backend, there is a subtle issue. The ADU devices are actually [explicitly blacklisted in the kernel](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/hid/hid-quirks.c?h=v5.4.88#n837) from being controlled by the `usbhid` driver (which in turn is used to present the devices used by the `hidraw` backend). Therefore, even if you manually unbind the driver from the `adutux` driver or unload the `adutux` driver altogether, the `usbhid` driver is precluded from taking control of the device.

There is a hacky workaround for this. The `usbhid` driver is capable of accepting module parameters that override the baked-in "quirks" settings. Unfortunately, this requires a reload of the `usbhid` driver, which could potentially cause other HID devices running on the system to malfunction (if they don't prevent the driver from being unloaded in the first place). Nonetheless, the following snippet shows how this workaround might be applied:

```console
# echo -n "<bus>-<device>:1.0" > /sys/bus/usb/drivers/adutux/unbind
# rmmod usbhid
# modprobe usbhid quirks=0x0a07:0x00da:0x40000000
```

The first command unbinds the device from being controlled by the `adutux` driver. The `<bus>` and `<device>` values in that command should be the USB bus and device address of the ADU device being used. The second command stops the `usbhid` driver from running so that it can be restarted with custom parameters. The third command restarts the `usbhid` module with a parameter that specifies additional "quirks" settings to be applied. In this command, `0x0a07:0x00da` is the USB VID:PID of the device whose quirks to adjust (may require tweaking depending on the specific device you're using), and `0x40000000` corresponds to `HID_QUIRK_NO_IGNORE`, which tells the `usbhid` not to ignore this device. Once the `usbhid` driver is loaded, it should automatically take control of the ADU device, and the Relacon software will be able to talk to it with the `hidraw` backend.
