# Third-party dependencies

Third-party source and binaries keep their upstream licenses and are read-only
from the product service.

| Path | Policy |
| --- | --- |
| `agent-studio-device-firmware/` | Pinned SDK snapshot used by this port |
| `agora-rtsa/` | Authorized local vendor package, ignored by Git |
| `manifests/` | Upstream commit and archive/hash provenance |

The current SDK is a source snapshot because the integration carries local
Linux transport patches. A later dependency-only change can replace it with the
official repository as a submodule at the same commit, provided those patches
move to a maintained patch set and offline release builds still have a
reproducible source path.

The RTSA binary is never committed. Supply it through `AGORA_SDK_DIR` and
`AGORA_RTC_LIBRARY`, and preserve the package and library hashes recorded in
`manifests/RTSA_SOURCE.txt`.
