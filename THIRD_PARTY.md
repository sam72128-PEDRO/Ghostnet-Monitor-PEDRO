# Vendored third-party code

Tracked as plain vendored source under `third_party/` for now. This is a
known gap, not a final decision — see PHASE1_PLAN.md, work stream 1.
Converting to git submodules (or another pinned-fetch mechanism) requires
network access to each upstream host, which this environment does not
currently allow; do it from a machine/session that can reach the upstream
remotes, and record the exact commit pinned at that time.

| Path | Upstream | License | Role | Notes |
|---|---|---|---|---|
| `third_party/js8core` | https://github.com/punk-kaos/js8core (unverified exact commit — pin when converting to submodule) | MIT | JS8 modem DSP, framing, decoder | Independent clean-room reimplementation, NOT derived from GPLv3 JS8Call desktop/Android source. README states FIR coefficients and lookup tables are generated, not copied. This is materially different from the GPLv3 "JS8Call-improved/Android-port" candidate evaluated in PHASE0_FINDINGS.md — that candidate was rejected/replaced by this MIT core at some point before this snapshot. The technical spec (section 4.1, 9.5) still assumes the GPLv3 port and needs updating to reflect this. |
| `third_party/libusb` | https://github.com/libusb/libusb | LGPL-2.1 | USB transport under Android `UsbDeviceConnection` fd | Permissive for dynamic linking; no viral obligation on the app as long as it's linked as a shared library and LGPL notices are preserved. |
| `third_party/rtl-sdr-blog` | https://github.com/rtlsdrblog/rtl-sdr-blog | GPLv2 | RTL2832U/R820T2 tuner control, Q-branch direct sampling | This is the one real copyleft constraint in the stack. If statically linked into the app binary, GPLv2 obligations (source offer, license notice) apply to the combined work. Decide early (WS2) whether to vendor this as a separate GPL-licensed shared component with a clear boundary, or accept GPLv2 terms for the whole release. |
| `third_party/Eigen` | https://gitlab.com/libeigen/eigen | MPL2 | Header-only linear algebra used by js8core | Header-only; kept as plain vendored snapshot even if other deps move to submodules. |
| `third_party/CRCpp` | https://github.com/d-bahr/CRCpp | MIT | CRC implementation used by js8core | Header-only, small, fine as a plain vendored snapshot. |

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
