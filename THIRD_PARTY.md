# Vendored third-party code

The app as a whole is GPL-2.0-or-later (`LICENSE`). Every component below
has a licence compatible with that: MIT, BSD-3-Clause, MPL-2.0 and
LGPL-2.1 code may be combined into a GPL-2.0-or-later work, and
`rtl-sdr-blog` is itself GPL-2.0-or-later.

Tracked as plain vendored source under `third_party/` for now. This is a
known gap, not a final decision — see PHASE1_PLAN.md, work stream 1.
Converting to git submodules (or another pinned-fetch mechanism) requires
network access to each upstream host, which this environment does not
currently allow; do it from a machine/session that can reach the upstream
remotes, and record the exact commit pinned at that time.

| Path | Upstream | License | Role | Notes |
|---|---|---|---|---|
| `third_party/js8core` | https://github.com/punk-kaos/js8core (unverified exact commit — pin when converting to submodule) | MIT | JS8 modem DSP, framing, decoder | Independent clean-room reimplementation, NOT derived from GPLv3 JS8Call desktop/Android source. README states FIR coefficients and lookup tables are generated, not copied. This is materially different from the GPLv3 "JS8Call-improved/Android-port" candidate evaluated in PHASE0_FINDINGS.md — that candidate was rejected/replaced by this MIT core at some point before this snapshot. The technical spec (section 4.1, 9.5) still assumes the GPLv3 port and needs updating to reflect this. |
| `third_party/libusb` | https://github.com/libusb/libusb (v1.0.30) | LGPL-2.1 | USB transport under Android `UsbDeviceConnection` fd | Currently linked **statically** into `libghostnet_native.so`. LGPL-2.1 permits that if users can relink (e.g. by providing object files or the app source). Since `rtl-sdr-blog` is GPLv2 and linked into the same library anyway, publishing the app's source under GPLv2 (WS2) satisfies both. |
| `third_party/rtl-sdr-blog` | https://github.com/rtlsdrblog/rtl-sdr-blog | GPLv2 | RTL2832U/R820T2 tuner control, Q-branch direct sampling | This is the one real copyleft constraint in the stack. If statically linked into the app binary, GPLv2 obligations (source offer, license notice) apply to the combined work. Decide early (WS2) whether to vendor this as a separate GPL-licensed shared component with a clear boundary, or accept GPLv2 terms for the whole release. |
| `third_party/Eigen` | https://gitlab.com/libeigen/eigen | MPL2 | Header-only linear algebra used by js8core | Header-only; kept as plain vendored snapshot even if other deps move to submodules. |
| `third_party/CRCpp` | https://github.com/d-bahr/CRCpp | BSD-3-Clause | CRC implementation used by js8core | Header-only, small, fine as a plain vendored snapshot. |

## References consulted (no code included)

| Project | Licence | How it was used |
|---|---|---|
| LinuxALE 0.0.x (Charles Brain, Ilkka Toivanen, 2001; sourceforge.net/projects/linuxale) | GPLv2 | Cross-check of the 2G ALE on-air format: tone plan and Gray mapping, Golay (24,12) generator, A/B interleave with inverted B check bits, stuff bit, triple redundancy. `rx-core/src/ale.cpp` is an independent implementation; the Golay table is generated from the generator polynomial, and the tests compare a few values against the published table. No LinuxALE code is included. |
| PC-ALE (github.com/alex-pennington/pc-ale @ 913edbd) | MIT | Evaluated and **rejected** as a reference: its Golay encode table holds 96 of 4096 entries (the rest are zero), it has no bit interleaving, and its tone plan (125 Hz spacing) does not match MIL-STD-188-141 (250 Hz). |

## Local modifications

Only one vendored file set is modified. Keep this list current; it's what
has to be re-applied when upstream is updated.

| Path | Change | Why |
|---|---|---|
| `third_party/rtl-sdr-blog/src/librtlsdr.c`, `include/rtl-sdr.h` | Split `rtlsdr_open()` into enumeration plus a shared `rtlsdr_open_common()`. Added `rtlsdr_open_sys_dev(dev, intptr_t fd)` using `libusb_init_context(LIBUSB_OPTION_NO_DEVICE_DISCOVERY)` + `libusb_wrap_sys_device()`. Marked "GhostNet Monitor local patch" in the source. | Android apps can't enumerate USB devices; they receive an opened file descriptor from `UsbManager`. This is libusb's documented pattern for that. The existing open path is unchanged in behaviour. GPLv2 applies to the modified file. |

## How they're built

None of the upstream build systems are used. `rx-core/cmake/libusb.cmake`
and `rx-core/cmake/rtlsdr.cmake` compile the needed sources directly. For
libusb that's the core plus the Linux usbfs backend (Android and Linux
hosts) or the Darwin backend (macOS hosts), with a generated `config.h`.
For librtlsdr it's the library only; none of the `rtl_*` tools are built.
js8core and Eigen are pulled in with `add_subdirectory`.

The Android app links only the receive path. js8core's engine contains
transmit code, but the app constructs it with no audio output, rig control
or network dependency and never calls a transmit method.
`rx-core/src/receiver.cpp` is the only place the engine is created. The
JS8 encoder (`legacy_encode`) is called only by host-side test tools; the
on-device self-test replays a precomputed tone table
(`rx-core/src/self_test_tones.inc`).

## Open items

- No exact pinned commit hash is recorded for any of these yet (none of them
  carry `.git` metadata in this snapshot). Before converting to submodules,
  re-fetch each from its upstream URL, verify license files still match the
  table above, and record the commit hash here.
- The `JS8Call-improved/Android-port` candidate referenced in
  `PHASE0_FINDINGS.md` (pinned commit `d0d1ccabb057f6e0e268e2f255e97cc52f821adf`)
  is GPLv3 and is **not** what's vendored here. Don't reintroduce it without
  redoing the license analysis — the whole point of moving to `js8core` was
  avoiding that GPLv3 surface for the decoder itself.
