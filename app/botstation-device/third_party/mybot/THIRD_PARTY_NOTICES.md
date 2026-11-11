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

The Agora RTSA vendor package is not included in this checkout. Builds require an authorized package
matching the target ABI and packet duration; supply its headers and library through
`AGORA_SDK_DIR` and `AGORA_RTC_LIBRARY`. See `../RTSA_SOURCE.txt` for the package provenance used
during development. No standalone license or NOTICE for the vendor binary was identified during
that review. Verify Agora's license, usage, and redistribution terms before using or distributing
the package. Files in an obtained Agora example package may carry their own copyright or license
headers; those terms also apply.

## cJSON-derived implementation

`src/support/mybot_json.c` and `src/internal/mybot_json.h` are namespaced derivatives of cJSON.
They retain the MIT license and Dave Gamble copyright notice in the source.

## xiaozhi-esp32 announcement assets

`assets/locales/` contains raw 16 kHz mono s16 PCM files decoded from the xiaozhi-esp32 project
(https://github.com/78/xiaozhi-esp32), which is distributed under the MIT License
(c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd. and Project Contributors. The pairing-code
prompt and digit sounds are derived from that project's `main/assets/locales/<locale>/*.ogg`
assets. The full MIT notice is included below and must accompany any redistribution of these files.

    MIT License

    Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
    Copyright (c) 2025 Project Contributors

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

The CMake install target installs this file as `share/doc/mybot/THIRD_PARTY_NOTICES.md`; it does not
install the optional root `assets/` PCM files. A product that copies those assets must copy this
notice with them.

## Release requirement

Publish a release artifact containing an Agora binary only after its license and written
redistribution authorization have been verified and retained in the release records. Otherwise,
exclude the binary and require users to supply `AGORA_SDK_DIR` and `AGORA_RTC_LIBRARY` locally.
