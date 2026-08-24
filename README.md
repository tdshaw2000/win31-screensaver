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
3. Copy `WIN31SS.SCR` into the Windows directory itself - `C:\WINDOWS`, not
   `C:\WINDOWS\SYSTEM` (that subdirectory is for DLLs/drivers, not
   screensavers; Control Panel won't find it there).
4. Open **Control Panel > Desktop**. It'll appear in the Screen Saver list as
   **SCRNSAVE : Win31SS** (see the note on the `SCRNSAVE :` marker below for
   why it's not just "Win31SS"). Use **Test** to run it fullscreen or
   **Setup** to open the config dialog stub.
5. To check preview-mode rendering, just select it in the list - the
   Desktop applet's preview box calls it with `/p <hwnd>` automatically.

Note: a `.SCR` won't run via File Manager's **File > Run** or double-click -
Windows resolves unrecognized extensions through a `WIN.INI` file
association, and `.SCR` isn't one of the built-in recognized executable
types (`.EXE`/`.COM`/`.BAT`/`.PIF`). This is normal for *any* `.SCR`,
including the ones bundled with Windows - it doesn't indicate a broken
build. Control Panel is the only intended entry point.

## Notes on Win16 / Windows 3.1 quirks

A few non-obvious things this project ran into, worth knowing before
debugging build or discovery issues again:

- **Control Panel's screensaver list is gated on a hidden marker, not the
  filename or a resource.** It only lists a `.SCR` whose NE
  non-resident-name-table entry (the module "description") starts with the
  literal text `SCRNSAVE :`. This isn't documented anywhere beyond vague old
  Microsoft KB articles ("Control Panel checks the header of each `.SCR`
  file for a specific code"). We found the exact requirement by pulling a
  stock Windows screensaver off the test VM and diffing its binary structure
  against ours with Wine's `winedump` tool. The Makefile sets this via a
  `wlink` `option description 'SCRNSAVE : Win31SS'` directive - it must
  come *after* the `system windows` and `name` directives in the `.lnk`
  file, or OpenWatcom's linker silently ignores it. A `STRINGTABLE`
  friendly-name resource (the modern Win95+/Win32 mechanism) was tried
  first and did *not* fix this - it's the wrong mechanism for Windows 3.1
  specifically.
- **OpenWatcom's `wlink` force-uppercases the NE name-table strings, with no
  option to disable it.** Both the module name (`name` directive) and the
  `option description` text get run through an unconditional `toupper()` -
  confirmed by reading `wlink`'s own source
  (`ResNonResNameTable()`/`WriteLoadU8Name()` in `bld/wl/c/loados2.c`,
  called with `ucase` hardcoded to `true`), not by trial and error. The
  original Microsoft `LINK.EXE` preserved case here, which is why stock
  screensavers show mixed-case names in Control Panel. Rebuilding `wlink`
  from a patched source tree isn't worth it for a cosmetic string, so the
  `Makefile` instead patches the linked `.exe` in place after `wlink`/`wrc`
  run: it locates the (same-length) uppercased text by byte search and
  rewrites it back to mixed case with `dd`, verified byte-for-byte to touch
  only those bytes and nothing else in the file.
- **`wcl` can't be used for linking in this OpenWatcom 2.0 build.** Passing
  a `.def` module-definition file to `wcl` makes it invoke the C compiler
  on the `.def` file itself (a real bug, not user error). That's why this
  project links with `wcc` (compile) + `wlink` (link) directly instead,
  mirroring OpenWatcom's own `samples/win/generic/win16` sample rather than
  using a `.def` file at all.
- **Some Win32-only APIs compile against the 16-bit headers with just a
  warning, then fail at link time.** `SetForegroundWindow` is one - it's
  Win32-only and doesn't exist in Win16, but the compiler only warns about a
  missing prototype rather than erroring; the actual failure only shows up
  as an `undefined reference` from the linker. Don't trust "it compiled" as
  proof an API is valid for Win16.
