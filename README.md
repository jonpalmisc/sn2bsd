# sn2bsd

Provides the BSD device path (`/dev/diskN`) for a USB device given its serial number.

```sh
$ sn2bsd 6578616D706C65
/dev/disk4
```

NOTE: Works on macOS only.

## Install

```sh
make install
```

Optionally set `PREFIX` before `make install` to control the destination.
