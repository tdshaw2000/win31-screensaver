# Win31SS

A Windows 3.1 screensaver, written in C89 against the Win16 API and
cross-compiled with OpenWatcom 2.0. Currently just the skeleton: command-line
handling, window/preview/config modes, a placeholder bouncing-circle
animation, and `CONTROL.INI` settings persistence. The real visual and a
proper settings dialog come later.

## What's a `.SCR` file?

A Windows screensaver is an ordinary Win16 executable (same MZ/NE format as
a `.EXE`) that Windows invokes with a specific set of command-line switches
instead of running normally. It's just renamed to `.SCR` and dropped into
the Windows directory so Control Panel's Desktop applet can find it.
`WIN31SS.SCR` in this repo *is* `win31ss.exe`, byte-for-byte, renamed.

## Command-line contract

Windows (and the Desktop control panel) invokes the `.SCR` with one of:

| Invocation      | Behavior                                                        |
|------------------|------------------------------------------------------------------|
| (no args), `/s`  | Run fullscreen. Any mouse move (after the first) or key press exits. |
| `/c`             | Show the configuration dialog (currently an empty stub).        |
| `/p <hwnd>`      | Embed a live preview into the given child window handle - this is what the Desktop applet's preview box uses. |
| `/a <hwnd>`      | Password-change entry point. Not supported; this is a no-op.    |

`ParseCmdLine` in `src/win31ss.c` implements this, accepting both `/` and
`-` switch prefixes case-insensitively.

Settings persist to `CONTROL.INI` (Windows 3.1 predates the registry) under
the `[ScreenSaver.Win31SS]` section, via `GetPrivateProfileInt` /
`WritePrivateProfileString`.

## Building locally

Requires an OpenWatcom 2.0 install with `WATCOM` pointing at it (e.g.
`/opt/watcom`, with `$WATCOM/binl` the directory holding the Linux-hosted
16-bit tools):

```sh
export WATCOM=/opt/watcom
make
```

This produces `WIN31SS.SCR` in the repo root. `make clean` removes build
artifacts.

Under the hood, the Makefile mirrors the build recipe OpenWatcom ships in
its own Win16 samples (`samples/win/generic/win16`): compile with `wcc`
using `-zW` (which generates the callback thunks Windows 3.x needs, so no
module-definition `EXPORTS` section is required), link with `wlink` via a
generated directive file, then bind the compiled `.res` resources into the
linked `.exe` with `wrc`.

If you don't have OpenWatcom installed locally, build inside the Docker
image instead (see below) - it's the same environment CI uses.

## Docker / CI

`Dockerfile` installs OpenWatcom 2.0 on `debian:12-slim` by downloading and
extracting the `ow-snapshot.tar.xz` release asset (a ready-built copy of
the `$WATCOM` tree) rather than running OpenWatcom's interactive GUI
installer, which doesn't work headlessly:

```sh
docker build -t win31ss-build .
docker run --rm -v "$(pwd)":/work -w /work win31ss-build make
```

`.gitlab-ci.yml` defines a single `build` stage that builds this image and
runs `make` inside it on every pipeline, publishing `WIN31SS.SCR` as a job
artifact. It rebuilds the image from the Dockerfile each run rather than
pulling a pre-built one from the GitLab Container Registry - slightly
slower (~15-30s to re-fetch the OpenWatcom snapshot) but guaranteed to
match what's committed, with no separate "rebuild and push the image" step
to remember. If commits get frequent enough for that cost to matter, switch
to building the image in its own job and pulling `$CI_REGISTRY_IMAGE`
instead. Because the runner's Docker daemon is a docker-in-docker sibling
container, the CI job can't bind-mount the checkout into it directly; it
copies the source in at image-build time (already handled by the
Dockerfile's `COPY`) and copies `WIN31SS.SCR` back out with `docker cp`
after running `make`.

## Testing in 86Box

1. Build `WIN31SS.SCR` as above.
2. Get it into the guest - either:
   - **Shared folder**: if your 86Box machine config has a host directory
     mounted as a network/shared drive, drop `WIN31SS.SCR` there and copy it
     from within the guest, or
   - **Virtual floppy**: create a blank `.img` (e.g. with 86Box's Tools >
     New Floppy Image, or `mkfs.msdos` on a raw image on the host), copy
     `WIN31SS.SCR` onto it with `mcopy` (from `mtools`) or by mounting it as
     a loop device, then attach it as a floppy image in 86Box and copy the
     file from `A:\` inside the guest.
3. Copy `WIN31SS.SCR` into the Windows directory (typically `C:\WINDOWS`).
4. Open **Control Panel > Desktop**, pick **Win31SS** from the Screen Saver
   list, and use **Test** to run it fullscreen or **Setup** to open the
   config dialog stub.
5. To check preview-mode rendering, just select it in the list - the
   Desktop applet's preview box calls it with `/p <hwnd>` automatically.
