# Dependency policy

## SDK snapshot

`third_party/agent-studio-device-firmware` is a pinned source snapshot of the
device SDK. Product code may include only its public `include/mybot/` headers.
The snapshot is read-only and its upstream licenses remain in place.

The snapshot currently includes local Linux transport diagnostics. Do not
replace it with an upstream checkout until those changes are either upstreamed
or carried as a maintained patch set. A later move to a submodule must preserve
offline release builds and the exact SDK and AOSL commits recorded under
`third_party/manifests/`.

## Agora RTSA

The target library is supplied externally and ignored by Git. Obtain an
authorized package with:

- AArch64 ELF64 shared-library ABI
- glibc-compatible symbols
- RTSA/RTM features required by the SDK
- packet cadence matching `MYBOT_AUDIO_PTIME_MS`
- G722 support matching the pinned SDK

The provenance file records the expected archive and library SHA256 values.
CMake validates ABI and cadence. Presence of a library alone does not grant
redistribution or product-use rights.

## Version changes

Upgrade the SDK and RTSA together when interfaces, codec or packet cadence
change. Update the manifests, run the complete host suite, compile all enabled
target features and record physical-device validation separately.
