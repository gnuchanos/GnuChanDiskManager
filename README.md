# GnuChan Disk Manager

GnuChan Disk Manager is an X11 disk and partition utility written in C. It
lists block devices and partitions in a parent/child tree, mounts and unmounts
selected filesystems, creates GPT or MBR partition tables, creates and deletes
partitions, and formats the selected disk or partition as ext4. The interface
shows device details, a visual partition bar that color-codes each filesystem
type, and a clearer action layout for common disk tasks. New partitions are
aligned by `parted`; their start and end are entered in MiB or GiB.

Before formatting, choose a local account. The ext4 format command uses the
`root_owner` extended option (supported since e2fsprogs 1.43) to set the root
directory owner to that account. When the Read or Write toggle is turned off,
the application temporarily mounts the new filesystem, applies `chmod` with the
requested mode, and unmounts. When both Read and Write are on, the ext4
default permissions (0755) already give the owner full access, so no extra
mount step is needed. Owner execute permission remains enabled so the selected
user can enter the directory. These permissions are stored in the filesystem
and remain when it is mounted again. No system-wide mount configuration is
written. Selecting a whole disk formats the entire device and destroys its
partition table; selecting a child formats only that partition.

## Mounting

- **Auto Mount** mounts the selected partition/disk (if it has a filesystem)
  using `udisksctl` when available, so the desktop session can attach it to the
  usual user media location automatically.
- If `udisksctl` is unavailable or cannot mount the device, the app falls back
  to a privileged `mount` into `/run/media/<user>/<label-or-name>`.
- **Unmount** uses `udisksctl` first and falls back to `umount` when needed.
- Already-mounted volumes are shown with a mount indicator in the device list.

## Safety and limits

- Destructive disk operations run through `pkexec` and the desktop's
  authentication prompt. Mount/unmount prefers desktop-friendly `udisksctl`
  and only falls back to privileged mount commands when necessary. No command is
  run through a shell.
- Formatting, deleting a partition, and creating a partition table require
  typing the exact device path. These operations destroy data.
- The Read and Write buttons are toggle buttons (shown with a checkbox
  indicator) that enable or disable those owner permissions for the next ext4
  format. Both are enabled by default. When both are on, the ext4 default
  mode 0755 is used; when either is off, a temporary mount+chmod applies the
  custom mode.
- Mounted partitions are not formatted or deleted. A disk with a mounted
  descendant is not repartitioned. Unmount volumes in the desktop first.
- The application limits destructive operations to writable disks with
  recognized physical/virtual disk names; read-only, loop, and zram devices
  are not accepted.
- The ownership selection applies to ext4 only. FAT/exFAT and other filesystems
  do not store ordinary Unix ownership in the filesystem; this release does
  not write per-volume mount settings for them.
- Resizing, moving, copying partitions, and other GParted features are not
  included in this initial release.

## Requirements

Build dependencies:

- Python 3, C11 compiler, and `pkg-config`
- X11 and Xft development files (`libx11-dev`, `libxft-dev` on Debian)

Runtime tools:

- X11 session, `lsblk`, `parted`, `pkexec`, and util-linux `mount`/`umount`
- `mkfs.ext4` from e2fsprogs
- `udisks2` and `polkit` for desktop file-manager mounting

The installer also places a polkit rule
(`/etc/polkit-1/rules.d/10-gnuchan-udisks2.rules`) that lets members of the
`sudo` group mount and manage internal (non-removable) filesystems through
udisks2 without an extra password prompt. Without this rule, desktop file
managers may report "not authorized to perform operation" when opening a
newly formatted internal disk.

## Build and test

```sh
python3 makefile.py build
python3 makefile.py test
python3 makefile.py run
```

Tests cover block-device output parsing, supported device names, partition
range and filesystem-label validation, parent-device traversal, mounted
descendants, and read-only device inventory.

## Install

```sh
python3 makefile.py
```

The installer asks for administrator permission and installs the binary,
desktop entry, and default configuration under `/usr/local`. It installs the
user configuration for the account that invoked `sudo` and preserves an
existing configuration. Run `python3 makefile.py uninstall` to remove the
installed program; the user's configuration is left alone.

## Configuration

The application reads
`$XDG_CONFIG_HOME/GnuChanDiskManager/config.conf` (normally
`~/.config/GnuChanDiskManager/config.conf`). The installed defaults are plain
`Name = value` settings in `src/config/config.conf`; supported settings are
`Title`, `Width`, `Height`, `FontSize`, the UI color names, and the partition
bar color names (`BarExt`, `BarNtfs`, `BarFat`, `BarSwap`, `BarOther`,
`BarUnalloc`).
