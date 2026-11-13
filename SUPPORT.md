# Support

[English](SUPPORT.md) | [简体中文](SUPPORT.zh-CN.md)

Start with [getting started](docs/user/getting-started.zh-CN.md), [troubleshooting](docs/user/troubleshooting.zh-CN.md) and the [validation matrix](docs/validation/README.md). This repository targets one recorded RV1126B board/system baseline; a successful host build does not establish compatibility with another firmware image.

For a bug report, use the repository issue template and include:

- Project version/commit and whether the tree was modified; voice or video profile and UI language.
- Board model, OS/kernel/libc, host environment and SDK/toolchain identity.
- Exact command, expected behavior, actual behavior and shortest reproduction.
- Whether the result came from compilation, a host test or a physical device; relevant sanitized logs.

Remove tokens, pairing codes, device IDs, serials, MAC addresses, private endpoints and personal paths before posting. Do not attach the KV directory or proprietary SDK archives. For a problem involving an external dependency, include its locked version and provenance rather than uploading the entire package.

Use issues for reproducible defects and concrete feature requests. For nonpublic service credentials or vendor SDK access, contact the service/vendor through your existing support channel. There is no response-time guarantee. A new board or long-running scenario remains unverified until evidence is recorded in the validation matrix.
