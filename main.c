#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/usb/IOUSBLib.h>

#include <sys/param.h>

#include <stdio.h>

static bool GetServiceBSDPath(io_service_t service, char *pathBuf, size_t pathSize)
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
            snprintf(pathBuf, pathSize, "/dev/%s", disk);
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

static bool GetServiceSerialNumber(io_service_t service, char *buf, size_t len)
{
    static char const *sKeys[] = {
        /*
         * Newer versions of macOS uses `kUSBSerialNumberString`, while older
         * ones use "USB Serial Number".
         */
        kUSBSerialNumberString,
        "USB Serial Number",
        NULL,
    };

    bool found = false;
    for (int i = 0; sKeys[i] && !found; i++) {
        CFStringRef key = NULL;
        CFTypeRef val = NULL;

        key = CFStringCreateWithCString(kCFAllocatorDefault, sKeys[i], kCFStringEncodingUTF8);
        if (!key) {
            goto L_cleanup;
        }

        val = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
        if (!val) {
            goto L_cleanup;
        }

        if (CFGetTypeID(val) == CFStringGetTypeID()) {
            found = CFStringGetCString((CFStringRef)val, buf, (CFIndex)len, kCFStringEncodingUTF8);
        }

    L_cleanup:
        if (val) {
            CFRelease(val);
        }
        if (key) {
            CFRelease(key);
        }
    }

    return found;
}

static bool GetBSDPathForUSBSerial(char const *serial, char *pathBuf, size_t pathSize)
{
    static char const *sClasses[] = {
        /* Newer macOS uses "IOUSBHostDevice" rather than "IOUSBDevice". */
        "IOUSBHostDevice",
        "IOUSBDevice",
        NULL,
    };
    bool found = false;

    for (int i = 0; sClasses[i] && !found; i++) {
        CFMutableDictionaryRef matching;
        io_iterator_t iter = IO_OBJECT_NULL;
        kern_return_t kr;
        io_service_t service;

        matching = IOServiceMatching(sClasses[i]);
        if (!matching) {
            goto L_cleanup;
        }

        kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);
        matching = NULL; /* Consumed by `IOServiceGetMatchingServices`. */
        if (kr != KERN_SUCCESS || iter == IO_OBJECT_NULL) {
            goto L_cleanup;
        }

        while (!found && (service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
            char currentSerial[256] = { 0 };

            if (GetServiceSerialNumber(service, currentSerial, sizeof(currentSerial))) {
                if (strcmp(currentSerial, serial) == 0) {
                    found = GetServiceBSDPath(service, pathBuf, pathSize);
                }
            }

            IOObjectRelease(service);
        }

    L_cleanup:
        if (iter) {
            IOObjectRelease(iter);
        }
        if (matching) {
            CFRelease(matching);
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

    char path[PATH_MAX];
    if (!GetBSDPathForUSBSerial(argv[1], path, sizeof(path))) {
        fprintf(stderr, "Error: No disk found for serial: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    printf("%s\n", path);

    return EXIT_SUCCESS;
}
