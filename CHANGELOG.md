# Changelog

Changes to project-maintained code follow Semantic Versioning. Validation evidence is maintained in [one matrix](docs/validation/README.md); a release entry does not imply hardware acceptance.

## 0.5.5 — Unreleased

- Disable the owned display output for an accepted UI poweroff instead of restoring a framebuffer that may already be gone; retain normal display restoration for ordinary stops.
- Preserve helper diagnostics after supervisor cleanup and prevent late shutdown signals from restoring the backlight after a successful system poweroff request.

Actual poweroff and physical screen darkness remain separate hardware acceptance steps.

## 0.5.4 — Unreleased

- Switch off and verify the board backlight after planned application cleanup and before normal system poweroff; restore its previous state if the transition fails.
- Keep normal application stop/restart display restoration, and validate the board backlight before deployment.
- Require regenerated supervised bundles containing the checked board poweroff helper.

Hardware poweroff and subsequent power-on remain separate acceptance steps.

## 0.5.3 — Unreleased

- Select video by default for supervised deployment; require explicit `video: false` for voice and show the selected profile in deployment plans.
- Derive compact hotspot names from the full permanent MAC and detect stopped AP services with a settings retry path.
- Clean owned Wi-Fi process groups and recover or report failed video pipelines with bounded retries.
- Validate rollback profiles, board prerequisites, startup modes and actual local application health before reporting deployment success.
- Separate portable build identity from local provenance and current dependency integrity from immutable import history.
- Centralize product lifecycle, use typed readiness state and document C API ownership and cleanup contracts.
- Maintain the portal as HTML with fixed failure categories, and generate matching localized runtime/licensing guides.
- Reproduce bilingual UI images and PDFs using committed fonts and a checked input/output manifest.
- Enforce complete Conventional Commit messages, document official dependency authorization and add private vulnerability reporting.

This candidate has its own source and artifact identity; it does not replace a differently sourced 0.5.2 installation in place. Hardware acceptance remains separate from host and cross-build checks.

## 0.5.2 — Unreleased

- Route manual Wi-Fi setup exclusively through the UI settings menu; remove touch and Menu holds, terminal `w`, and the forced-provisioning CLI option. Automatic offline setup remains available.
- Select the default service by UI build language: Shengwang for `zh-CN` and Agora for `en-US`, with CLI and environment overrides taking precedence.

## 0.5.1 — Unreleased

- Keep current documentation versions synchronized with CMake, render package language examples, and enforce the pinned formatter before quality checks.
- Clarify direct application execution versus supervised system deployment and correct lifecycle diagnostics.
- Add a persistent settings entry with Wi-Fi setup, poweroff confirmation, cancellation and failure pages in both UI languages.
- Place settings navigation at the top left and retain multitouch slot coordinates across contacts for repeated taps.
- Keep product input available before and across SDK sessions, normalize touchscreen coordinates and isolate menu gestures from conversation controls.
- Request planned poweroff through an authenticated supervisor transaction; shut down only after successful application and descendant cleanup.

Actual device power removal and restart behavior require hardware acceptance.

## 0.4.4 — Unreleased

- Implement the Wi-Fi backend, profile storage, captive portal and controller in C99 with explicit resource ownership and pthread synchronization.
- Separate supplicant control, process ownership and AP radio selection behind private C interfaces.
- Preserve provisioning, committed lease verification, recovery and public C APIs; migrate Wi-Fi regression tests to C.

Hardware regression evidence is recorded in the validation matrix.

## 0.4.3 — Unreleased

- Reselect the committed network when a system Wi-Fi reload resumes another saved profile.
- Request renewal from the existing DHCP manager when the selected network still has a stale lease event, with a bounded setup confirmation window.

Phone setup and business recovery require hardware regression testing for this candidate.

## 0.4.2 — Unreleased

- Resume network recovery while waiting to start the SDK after Wi-Fi provisioning, including a transient route loss after the portal closes.
- Preserve the system supplicant control path and reconnect its control socket when a configuration reload replaces it.
- Verify the committed network association and routed lease before reporting successful setup to the phone.

Phone setup and business recovery require hardware regression testing for this candidate.

## 0.4.1 — Unreleased

- Select AP band/channel from the current station frequency and that radio's advertised concurrency instead of rejecting every 5 GHz station.
- Prefer a 2.4 GHz AP on radios supporting multiple channels; use supported non-DFS station channels for a single-channel 5 GHz AP.
- Keep 6 GHz distinct from 2.4 GHz when channel numbers overlap, and retain explicit failure for unsupported radio combinations.

Device AP and phone interoperability remain subject to verification.

## 0.4.0 — Unreleased

- Add explicit versioned device deployment, private backups, binding identity checks and rollback without changing the existing data directory.
- Add a BusyBox init service and a standalone supervisor with bounded restart, log rotation and owned descendant cleanup.
- Integrate system Wi-Fi profiles and dhcpcd lease events; preserve committed station networks across application restarts.
- Request SDK shutdown before saved-network recovery or AP entry after a sustained route loss.
- Publish a private application health record and package the service, DHCP hook and supervisor with provenance checks.

This deployment candidate requires user verification on the device, including restart, AP setup and audio regression.

## 0.3.3 — Unreleased

- Gate SDK startup on plausible system time after network readiness and AP exit, with a configurable 1–300-second monotonic timeout, defaulting to 30 seconds.
- Preserve OS time services by default; report timeout and helper failures through the UI and standard error, then exit with a nonzero status.
- Support an explicitly selected foreground time helper only when the clock is invalid and no existing time daemon is detected; cancel and reap its owned process group on network loss or shutdown.
- Bundle an opt-in BusyBox one-shot wrapper with validated NTP server configuration, without installing services or disabling TLS verification.
- Reset the wait window on network loss and reject successful helper exits that leave an invalid clock; the 2026 threshold remains a plausibility check, not proof of precise synchronization.

This system-time guard candidate requires hardware clock, service-ownership and NTP connectivity testing.

## 0.3.2 — Unreleased

- Play the bundled localized Wi-Fi guidance once per setup session, only after the AP and portal are ready; connection retries do not repeat it.
- Apply saved output volume to guidance, including mute, through the same playback adapter used by MyBot.
- Keep the prompt worker mutually exclusive with SDK audio; cancel and release playback when leaving AP setup or exiting before restarting MyBot.
- Report guidance failures without disabling the AP portal, and document target checks for complete playback, retry behavior and audio cleanup.

This AP voice-guidance candidate requires hardware audio and Wi-Fi regression testing.

## 0.3.1 — Unreleased

- Connect MyBot device-volume controls to a platform playback-gain adapter, applying output gain once without changing capture gain or system mixer settings.
- Persist volume across application and SDK restarts, including manual AP Wi-Fi setup, in a private atomic settings file.
- Preserve existing key, terminal and cloud controls with a 0–100 range and a default of 100, matching the previous output level.
- Fall back to the default with a warning for invalid settings; report save failures and retain the last working gain.

This persistent-volume candidate requires hardware audio and Cloud AEC regression testing.

## 0.3.0 — Unreleased

- Add native AP Wi-Fi setup with an open `mybot-xxxx` hotspot, captive DNS and a bilingual mobile page at `192.168.4.1`.
- Support nearby and hidden networks, asynchronous connection/retry, saved-network default/deletion, and up to 10 private profiles containing derived PSKs.
- Preserve an existing usable network; try cached profiles before AP setup, and persist candidates only after routed IPv4 connectivity is available.
- Add 3-second touch/Menu hold and terminal `w` for manual setup, stopping MyBot before AP entry and restarting it after completion.
- Add explicit Wi-Fi interface/force/disable CLI options, AP+STA preference with safe station ownership checks, and a bundled DHCP hook. Target-side network tools and driver support remain required.
- Add host controller/backend/protocol checks, input/UI coverage and a hardware verification guide.

This AP Wi-Fi setup candidate requires hardware testing; host and cross-build evidence does not establish target radio or DHCP behavior.

## 0.2.2 — Unreleased

- Group maintained code under `src/application/`, `src/platform/` and `src/ui/`, and pinned dependencies under `vendor/`.
- Synchronize CMake, build/check/package automation, dependency mappings and documentation with the new paths.
- Preserve public include namespaces, CMake targets, generated fonts and dependency bytes, including all 99 official RTSA files.
- Keep developer programs in `tools/` and automation in `scripts/` with explicit ownership.

This source-layout candidate requires its own hardware regression.

## 0.2.1 — Unreleased

- Manage MyBot, AOSL and LVGL as separate pinned source directories.
- Record directory migrations, explicit omissions, unique ownership and required build inputs in the dependency lock.
- Keep the Linux production subset, move integration documentation outside dependency sources, and preserve runtime sources and licenses.
- Vendor the complete official Agora RTSA 1.10.1 package as an essential independent dependency; pin target builds to its AArch64 library and include vendor documentation in application bundles.
- Enforce Conventional Commits with a repository hook, message template and CI validation.

This dependency-layout candidate requires its own hardware regression.

## 0.2.0 — Unreleased

- Organize application configuration, process guards, board defaults, Linux drivers/services and pure UI into explicit components.
- Add isolated CMake presets, explicit SDK preparation, dependency provenance checks and host CI entry points.
- Bind build configuration and binary identity to manifests, verify selected package profiles and keep generated artifacts outside source control.
- Add user/developer guides, contribution/support templates and release guidance; archive original migration records.
- Preserve the 0.1.1 CLI, identity/KV contract, 20 ms audio configuration and native pixel-glasses rendering as the compatibility baseline.

This governance candidate requires a new hardware regression. Optional video, audio-quality measurements and extended concurrent operation remain pending.

## 0.1.1 — 2026-11-13

- Render at native 640×480 and rotate for the physical screen.
- Generate target-size 18/20/40/64 px fonts and adjust layout while retaining pixel-glasses geometry.
- Add text bounds, glyph coverage and pixel-repetition regression checks.
- User confirmed the improved display meets requirements on the target device.

## 0.1.0 — 2026-11-13

- Initial MyBot 1.3.0 RV1126B Linux application: HTTPS pairing, bidirectional voice, persistent identity, evdev input and pixel-glasses UI.
- Optional V4L2/MPP JPEG uplink, AArch64 ABI checks and self-contained test bundles.
- User verified checksums and the default voice/UI business flow on hardware. Optional video and extended stability were not accepted by that result.
