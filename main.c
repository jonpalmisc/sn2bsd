//
//  sn2bsd
//  https://github.com/jonpalmisc/sn2bsd
//
//  Copyright (c) 2026 Jon Palmisciano. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
//  3. Neither the name of the copyright holder nor the names of its
//     contributors may be used to endorse or promote products derived from
//     this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//  CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE)
//  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  POSSIBILITY OF SUCH DAMAGE.
//

#include <IOKit/IOBSD.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/usb/IOUSBLib.h>

#include <sys/param.h>

#include <stdbool.h>
#include <stdio.h>

#define countof(a) (sizeof(a) / sizeof(*(a)))

/// Get the BSD path for a registry entry (if applicable).
///
/// Returns true if the path was successfully obtained and written to the buffer.
static bool TryGetEntryBSDPath(io_registry_entry_t entry, char *pathBuf, size_t pathLen) {
    if (!IOObjectConformsTo(entry, kIOMediaClass)) {
        return false;
    }

    CFTypeRef name = IORegistryEntryCreateCFProperty(entry, CFSTR(kIOBSDNameKey), NULL, 0);
    if (!name) {
        return false;
    }

    if (CFGetTypeID(name) == CFStringGetTypeID()) {
        char disk[64]; // BSD device names are short; a large buffer is overkill.

        bool found = CFStringGetFileSystemRepresentation(name, disk, sizeof(disk));
        if (found) {
            snprintf(pathBuf, pathLen, "/dev/%s", disk);
        }
    }

    CFRelease(name);

    return true;
}

/// Get the BSD path for a service (if applicable).
///
/// Returns true if the path was successfully obtained and written to the buffer.
static bool GetServiceBSDPath(io_service_t service, char *pathBuf, size_t pathLen) {
    io_iterator_t iter;
    kern_return_t kr = IORegistryEntryCreateIterator(
        service,
        kIOServicePlane,
        kIORegistryIterateRecursively,
        &iter
    );
    if (kr != KERN_SUCCESS || iter == IO_OBJECT_NULL) {
        return false;
    }

    bool found = false;

    io_registry_entry_t entry;
    while (!found && (entry = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        found = TryGetEntryBSDPath(entry, pathBuf, pathLen);

        IOObjectRelease(entry);
    }

    IOObjectRelease(iter);

    return found;
}

/// Get the serial number (if applicable) associated with a service.
///
/// Returns true if serial number was successfully obtained and written to the
/// output buffer.
static bool GetServiceSerialNumber(io_service_t service, char *serialBuf, size_t serialLen) {
    static CFStringRef const sKeys[] = {
        CFSTR(kUSBSerialNumberString), // Used on newer macOS.
        CFSTR("USB Serial Number"),    // Used on older macOS.
    };

    bool found = false;

    for (size_t i = 0; !found && i < countof(sKeys); i++) {
        CFTypeRef val = IORegistryEntryCreateCFProperty(service, sKeys[i], NULL, 0);
        if (!val) {
            continue;
        }

        if (CFGetTypeID(val) == CFStringGetTypeID()) {
            found = CFStringGetCString(val, serialBuf, (CFIndex)serialLen, kCFStringEncodingUTF8);
        }

        CFRelease(val);
    }

    return found;
}

/// Get the BSD path for the USB device matching the given serial number.
///
/// Returns true if a matching device was found and its path was written to the
/// output buffer.
static bool GetBSDPathForUSBSerial(char const *serial, char *pathBuf, size_t pathLen) {
    static char const *const sClasses[] = {
        "IOUSBHostDevice", // Used on newer macOS.
        "IOUSBDevice",     // Used on older macOS.
    };

    bool found = false;

    for (size_t i = 0; !found && i < countof(sClasses); i++) {
        CFMutableDictionaryRef matching = IOServiceMatching(sClasses[i]);
        if (!matching) {
            continue;
        }

        io_iterator_t iter = IO_OBJECT_NULL;
        kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);
        if (kr != KERN_SUCCESS || iter == IO_OBJECT_NULL) {
            // Matching dictionary does not need to be released as it has
            // already been consumed by `IOServiceGetMatchingServices`.
            continue;
        }

        io_service_t service;
        while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
            char serviceSerial[256];
            bool gotSerial = GetServiceSerialNumber(service, serviceSerial, sizeof(serviceSerial));
            bool serialMatch = gotSerial && strcmp(serviceSerial, serial) == 0;

            found = serialMatch && GetServiceBSDPath(service, pathBuf, pathLen);

            IOObjectRelease(service);
        }

        IOObjectRelease(iter);
    }

    return found;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <serial>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char bsdPath[PATH_MAX];
    if (!GetBSDPathForUSBSerial(argv[1], bsdPath, sizeof(bsdPath))) {
        fprintf(stderr, "Error: No disk found for serial: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    printf("%s\n", bsdPath);

    return EXIT_SUCCESS;
}
