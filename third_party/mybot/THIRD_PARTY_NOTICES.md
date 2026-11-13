# Third-party notices

The root Apache-2.0 license applies to mybot-maintained code unless a file states otherwise. It does
not replace third-party terms. This file is informational, not legal advice.

## AOSL

Location: `third_party/aosl` — a git submodule pinned to upstream commit
`84e086084ebcd0ae2455a0ce5721950c5fe2e656` of https://github.com/AgoraIO-Community/aosl.

AOSL includes `third_party/aosl/LICENSE`, which is based on Apache-2.0 and adds restrictive
conditions. Read that file before using, modifying, deploying, or redistributing AOSL. Do not label
the combined repository or binary as uniformly Apache-2.0.

GitHub's automatic source archives omit AOSL. After a Git clone, initialize it with
`git submodule update --init --recursive`; this command cannot populate an archive without `.git`.
The source asset produced by `scripts/release.sh package TAG` includes the tag's pinned AOSL
contents and license. See [the release guide](docs/RELEASING.md) for archive usage.

## Agora RTSA SDK

Location: `third_party/agora_rtsa_sdk`. The bundled shared-library package identifies itself as
`Agora-RTSALite-RmRcAcAj-x86_64-linux-gnu-v1.10.1-20260828_180234-1270765`. It is provided for
Linux **development and demo** use only and is intentionally retained in this repository for the
Linux reference demo (it is not removed).

The vendored package retains only `PACKAGE_INFO`, the public API header, `.config`, and the
shared library. Vendor examples, skills, demo/test assets, and helper binaries are not part of this
repository.

Package provenance recorded for this update:

- Source archive: `Agora-RTSALite-RmRcAcAj-x86_64-linux-gnu-v1.10.1-20260828_180234-1270765.tgz`
- Archive SHA-256: `739901d09a4161a33f36e06281fe30dc11629f1f3271e55a5662501593985fbf`
- `libagora-rtc-sdk.so` SHA-256: `269d985f2fcb86ce259591853e59bc5a5d86f5f8fa82a1408bc7ac3795c3f999`

No standalone license or NOTICE for the bundled RTSA binary was found in the package during the
1.1.0 audit. Possession of the files is not evidence of redistribution rights. Commercial or
production use and redistribution (source archives, binaries, container/firmware images, mirrors)
require a separate license from Agora (声网). Release maintainers must retain the applicable terms
and written redistribution authorization in the release records before publishing an artifact that
contains the binary; the presence of this repository copy does not itself record that authorization.

## cJSON-derived implementation

`src/support/mybot_json.c` and `src/internal/mybot_json.h` are namespaced derivatives of cJSON.
They retain the MIT license and Dave Gamble copyright notice in the source.

## Release requirement

Publish a release artifact containing the bundled Agora binary only after its license and written
redistribution authorization have been verified and retained in the release records. If that record
is unavailable, exclude the binary and require users to supply `AGORA_SDK_DIR` and
`AGORA_RTC_LIBRARY` locally.
