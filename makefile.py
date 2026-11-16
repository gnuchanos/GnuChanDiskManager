#!/usr/bin/env python3
"""Build, test, run, install, or uninstall GnuChanDiskManager."""

from __future__ import annotations

import os
import pwd
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT.parent / "_temp" / "gnuchandiskmanager-build"
PROGRAM = "GnuChanDiskManager"
BIN_DIR = Path("/usr/local/bin")
APPS_DIR = Path("/usr/local/share/applications")
DATA_DIR = Path("/usr/local/share/gnuchandiskmanager")
CONFIG_SOURCE = ROOT / "src" / "config" / "config.conf"
DESKTOP_SOURCE = ROOT / "gnuchandiskmanager.desktop"
POLKIT_SOURCE = ROOT / "src" / "polkit" / "10-gnuchan-udisks2.rules"
POLKIT_DIR = Path("/etc/polkit-1/rules.d")
SOURCES = (ROOT / "src" / "main.c", ROOT / "src" / "disk_ops.c")
TEST_SOURCE = ROOT / "tests" / "test_disk_ops.c"
ELEVATED_VARIABLE = "GNUCHANDISKMANAGER_ELEVATED"


def step(message: str) -> None:
    print(f"==> {message}", flush=True)


def detail(message: str) -> None:
    print(f"    {message}", flush=True)


def run(
    command: list[str],
    *,
    capture: bool = False,
    cwd: Path = ROOT,
    environment: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=False,
        cwd=cwd,
        env=environment,
        text=True,
        capture_output=capture,
    )


def is_root() -> bool:
    return os.geteuid() == 0


def ensure_root() -> None:
    if is_root():
        return
    if os.environ.get(ELEVATED_VARIABLE) == "1":
        raise SystemExit("error: still not root after sudo")
    sudo = shutil.which("sudo")
    if sudo is None:
        raise SystemExit("error: this installs into /usr/local; run as root")
    step("Needs root; re-running through sudo")
    environment = dict(os.environ)
    environment[ELEVATED_VARIABLE] = "1"
    os.execvpe(sudo, [sudo, sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]],
               environment)


def compiler_flags() -> tuple[list[str], list[str]]:
    pkg_config = shutil.which("pkg-config")
    if pkg_config is None:
        raise SystemExit("error: pkg-config is required; install pkg-config")
    cflags = run([pkg_config, "--cflags", "x11", "xft"], capture=True)
    libs = run([pkg_config, "--libs", "x11", "xft"], capture=True)
    if cflags.returncode != 0 or libs.returncode != 0:
        raise SystemExit(
            "error: X11/Xft development files are required; "
            "on Debian install libx11-dev and libxft-dev"
        )
    return cflags.stdout.split(), libs.stdout.split()


def check_sources() -> None:
    missing = [str(path.relative_to(ROOT)) for path in (*SOURCES, TEST_SOURCE, CONFIG_SOURCE, DESKTOP_SOURCE, POLKIT_SOURCE)
               if not path.is_file()]
    if missing:
        raise SystemExit("error: missing project files: " + ", ".join(missing))


def build(*, test: bool = False) -> Path:
    check_sources()
    BUILD.mkdir(parents=True, exist_ok=True)
    output = BUILD / ("test_disk_ops" if test else PROGRAM)
    cflags, libs = compiler_flags()
    sources = [str(TEST_SOURCE), str(SOURCES[1])] if test else [
        str(source) for source in SOURCES
    ]
    command = [
        shutil.which("cc") or "cc",
        "-std=c11",
        "-D_GNU_SOURCE",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "-O2",
        *cflags,
        "-I",
        str(ROOT / "src"),
        *sources,
        "-o",
        str(output),
        *([] if test else libs),
    ]
    step("Building tests" if test else "Building")
    result = run(command)
    if result.returncode != 0:
        raise SystemExit("error: the build failed")
    detail(f"built {output}")
    return output


def invoking_user() -> tuple[Path, int, int] | None:
    name = os.environ.get("SUDO_USER")
    if not name:
        return None
    try:
        user = pwd.getpwnam(name)
    except KeyError:
        return None
    return Path(user.pw_dir), user.pw_uid, user.pw_gid


def install(binary: Path) -> None:
    ensure_root()
    step("Installing")
    BIN_DIR.mkdir(parents=True, exist_ok=True)
    APPS_DIR.mkdir(parents=True, exist_ok=True)
    DATA_DIR.mkdir(parents=True, exist_ok=True)

    installed_binary = BIN_DIR / PROGRAM
    staged_binary = BIN_DIR / f"{PROGRAM}.new"
    shutil.copyfile(binary, staged_binary)
    staged_binary.chmod(0o755)
    os.replace(staged_binary, installed_binary)
    detail(f"installed {installed_binary}")

    installed_desktop = APPS_DIR / DESKTOP_SOURCE.name
    desktop = DESKTOP_SOURCE.read_text(encoding="utf-8")
    desktop = desktop.replace("Exec=GnuChanDiskManager",
                              f"Exec={installed_binary}")
    desktop = desktop.replace("TryExec=GnuChanDiskManager",
                              f"TryExec={installed_binary}")
    staged_desktop = installed_desktop.with_name(installed_desktop.name + ".new")
    staged_desktop.write_text(desktop, encoding="utf-8")
    staged_desktop.chmod(0o644)
    os.replace(staged_desktop, installed_desktop)
    detail(f"installed {installed_desktop}")

    POLKIT_DIR.mkdir(parents=True, exist_ok=True)
    installed_polkit = POLKIT_DIR / POLKIT_SOURCE.name
    shutil.copyfile(POLKIT_SOURCE, installed_polkit)
    installed_polkit.chmod(0o644)
    detail(f"installed polkit rule {installed_polkit}")

    installed_config = DATA_DIR / "config.conf"
    shutil.copyfile(CONFIG_SOURCE, installed_config)
    installed_config.chmod(0o644)
    detail(f"installed defaults {installed_config}")

    version = ROOT / "version.txt"
    if version.is_file():
        shutil.copyfile(version, DATA_DIR / "version.txt")

    user = invoking_user()
    if user is None:
        detail("skipped the user config: no invoking login user")
        return
    home, uid, gid = user
    config_directory = home / ".config" / "GnuChanDiskManager"
    config_directory.mkdir(parents=True, exist_ok=True)
    user_config = config_directory / "config.conf"
    if user_config.exists():
        detail(f"kept the existing {user_config}")
    else:
        shutil.copyfile(CONFIG_SOURCE, user_config)
        user_config.chmod(0o644)
        detail(f"installed {user_config}")
    os.chown(config_directory, uid, gid)
    os.chown(user_config, uid, gid)


def uninstall() -> None:
    ensure_root()
    step("Uninstalling")
    for path in (
        BIN_DIR / PROGRAM,
        APPS_DIR / DESKTOP_SOURCE.name,
        DATA_DIR / "config.conf",
        DATA_DIR / "version.txt",
        POLKIT_DIR / POLKIT_SOURCE.name,
    ):
        if path.exists():
            path.unlink()
            detail(f"removed {path}")
    try:
        DATA_DIR.rmdir()
    except FileNotFoundError:
        pass
    except OSError:
        detail(f"kept non-empty {DATA_DIR}")
    note = "The user's ~/.config/GnuChanDiskManager/ settings were left alone."
    detail(note)


def main() -> int:
    actions = ("install", "build", "test", "run", "uninstall")
    action = sys.argv[1] if len(sys.argv) > 1 else "install"
    if len(sys.argv) > 2 or action not in actions:
        print(
            "usage: python3 makefile.py [install|build|test|run|uninstall]",
            file=sys.stderr,
        )
        return 2
    if action == "uninstall":
        uninstall()
        return 0

    binary = build(test=action == "test")
    if action == "test":
        step("Running tests")
        result = run([str(binary)])
        return result.returncode
    if action == "build":
        return 0
    if action == "run":
        if not os.environ.get("DISPLAY"):
            raise SystemExit("error: DISPLAY is not set; an X11 session is required")
        return run([str(binary)]).returncode
    install(binary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
