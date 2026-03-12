#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/usb/IOUSBLib.h>

#include <sys/param.h>

#include <stdio.h>

/**
 * Get the BSD path for a service (if applicable).
 *
 * Returns true if the path was successfully obtained and written to the buffer.
 */
static bool GetServiceBSDPath(io_service_t service, char *pathBuf, size_t pathLen)
{
    io_iterator_t iter;
    kern_return_t kr = IORegistryEntryCreateIterator(service,
                                                     kIOServicePlane,
                                                     kIORegistryIterateRecursively,
                                                     &iter);
    if (kr != KERN_SUCCESS || iter == IO_OBJECT_NULL) {
        return false;
    }

    bool found = false;
    io_registry_entry_t entry;
    while (!found && (entry = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        CFTypeRef name = NULL;

        if (!IOObjectConformsTo(entry, kIOMediaClass)) {
            goto L_cleanup;
        }

        name = IORegistryEntryCreateCFProperty(entry, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
        if (!name || CFGetTypeID(name) != CFStringGetTypeID()) {
            goto L_cleanup;
        }

        /*
         * We're expecting a short path here, which is why this isn't PATH_MAX.
         */
        char disk[64];
        if (CFStringGetFileSystemRepresentation(name, disk, sizeof(disk))) {
            snprintf(pathBuf, pathLen, "/dev/%s", disk);
            found = true;
        }

    L_cleanup:
        if (name) {
            CFRelease(name);
        }

        IOObjectRelease(entry);
    }

    IOObjectRelease(iter);
    return found;
}

/**
 * Get the serial number (if applicable) associated with a service.
 *
 * Returns true if serial number was successfully obtained and written to the
 * output buffer.
 */
static bool GetServiceSerialNumber(io_service_t service, char *serialBuf, size_t serialLen)
{
    static CFStringRef sKeys[] = {
        CFSTR(kUSBSerialNumberString), /* Used on newer macOS. */
        CFSTR("USB Serial Number"),    /* Used on older macOS. */
    };
    static size_t sKeysLen = sizeof(sKeys) / sizeof(*sKeys);

    bool found = false;
    for (size_t i = 0; i < sKeysLen && !found; i++) {
        CFStringRef key = sKeys[i];
        CFTypeRef val = NULL;

        val = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
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

static bool GetBSDPathForUSBSerial(char const *serial, char *pathBuf, size_t pathSize)
{
    static char const *sClasses[] = {
        "IOUSBHostDevice", /* Used on newer macOS. */
        "IOUSBDevice",     /* Used on older macOS. */
    };
    static size_t const sClassesLen = sizeof(sClasses) / sizeof(*sClasses);

    bool found = false;
    for (size_t i = 0; i < sClassesLen && !found; i++) {
        CFMutableDictionaryRef matching = IOServiceMatching(sClasses[i]);
        if (!matching) {
            continue;
        }

        io_iterator_t iter;
        kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);
        if (kr != KERN_SUCCESS || iter == IO_OBJECT_NULL) {
            /*
             * Matching dictionary does not need to be released as it has
             * already been consumed by `IOServiceGetMatchingServices`.
             */
            continue;
        }

        io_service_t service;
        while (!found && (service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
            char serviceSerial[256] = { 0 };

            if (GetServiceSerialNumber(service, serviceSerial, sizeof(serviceSerial))) {
                if (strcmp(serviceSerial, serial) == 0) {
                    found = GetServiceBSDPath(service, pathBuf, pathSize);
                }
            }

            IOObjectRelease(service);
        }

        if (iter) {
            IOObjectRelease(iter);
        }
    }

    return found;
}

int main(int argc, char *argv[])
{
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
