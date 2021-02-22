cmake_minimum_required(VERSION 3.10)

project(Relacon VERSION 0.1)

# This project is currently only intended to be built under Unix-like
# environments. It has only been tested to build under Linux and MSYS2.
if (!UNIX)
    message(SEND_ERROR "Unsupported build environment ${CMAKE_SYSTEM_NAME}")
endif()

# Using pkgconfig to locate library dependencies
find_package(PkgConfig REQUIRED)

# Add supplemental warnings and treat warnings as errors
add_compile_options(-Wall -Wextra -pedantic -Werror -Wformat -Wundef)

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
    add_compile_definitions(DEBUG)
endif()

#
# Unfortunately, due to a bug in the ADU218 firmware's string descriptor
# handling, libusb doesn't work for querying the string descriptors in Windows,
# and HIDAPI doesn't work for querying the string descriptors in Linux. Thus,
# even though either library should ideally work on either OS, we have to pick
# and choose based on how the underlying implementation requests the string
# descriptors. See the README for additional information.
#
if (WIN32)
    set(BACKEND "hidapi")
else()
    set(BACKEND "libusb")
endif()

if(BACKEND STREQUAL "hidapi")
    pkg_search_module(HIDAPI REQUIRED IMPORTED_TARGET hidapi hidapi-libusb hidapi-hidraw)
    set(BACKEND_LIBS PkgConfig::HIDAPI)
    set(BACKEND_SRCS src/BackendHidapi.c)
elseif(BACKEND STREQUAL "libusb")
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    set(BACKEND_LIBS PkgConfig::LIBUSB)
    set(BACKEND_SRCS src/BackendLibusb.c)
else()
    message(SEND_ERROR "Unexpected backend: ${BACKEND}")
endif()

# Select the source files used to compile the library
set(RELACON_LIBRARY_SRCS
    src/Relacon.c
    src/DeviceCapabilities.c
    src/Log.c
    ${BACKEND_SRCS}
)

# Define the default logging level for the Relacon library
set(RELACON_DEFAULT_LOG_LEVEL LOG_LEVEL_WARNING)

# Create a library that can be shared by any frontend applications
add_library(relacon ${RELACON_LIBRARY_SRCS})
target_include_directories(relacon PUBLIC include)
target_link_libraries(relacon ${BACKEND_LIBS})
target_compile_definitions(relacon PRIVATE LOG_THRESHOLD_DEFAULT=${RELACON_DEFAULT_LOG_LEVEL})

if (WIN32)
    #
    # On Windows, HIDAPI will enumerate a device for every HID collection on a
    # device whereas on Linux all HID collections are encompassed in a single
    # device. Hence, to select the correct device instance on Windows, we must
    # filter based on the collections' usage identifier
    #
    target_compile_definitions(relacon PRIVATE FILTER_HID_COLLECTION_USAGE)
endif()

# Create a command-line interface application
add_executable(relacon-cli src/relacon-cli.c)
target_compile_definitions(relacon-cli PRIVATE RELACON_CLI_VERSION="${CMAKE_PROJECT_VERSION}")
target_link_libraries(relacon-cli PRIVATE relacon)

#
# Installation rules
#
include(GNUInstallDirs)
install(TARGETS relacon-cli)
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    # On Linux, install udev rules
    set(UDEV_RULES_DIR ${CMAKE_INSTALL_SYSCONFDIR}/dev/rules.d)
    install(FILES udev/99-relacon.rules DESTINATION ${UDEV_RULES_DIR})
endif()