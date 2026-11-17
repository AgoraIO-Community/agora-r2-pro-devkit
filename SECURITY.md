# Security policy

[English](SECURITY.md) | [简体中文](SECURITY.zh-CN.md)

Report suspected security vulnerabilities privately to **junlon2006@163.com**.
Do not open a public issue containing credentials, exploitation details or private
device data. This address is a reporting channel; no response-time commitment is
made.

Include the affected version/commit, board and firmware baseline, build profile,
reproduction steps, impact and a minimal sanitized example. Avoid sending tokens,
Wi-Fi passwords or PSKs, pairing codes, complete KV directories, private SDK
archives or device identifiers. Describe any sensitive evidence first so that the
maintainer can arrange an appropriate exchange if needed.

Security fixes are developed on `dev` and included in the next tagged release.
The latest tagged release, when one exists, and current `dev` are the supported reporting targets;
older releases are assessed when the issue also affects a supported target.
Backports and a fixed support period are not promised. Deployment candidates and
hardware coverage are identified in the [validation status](docs/validation/README.md).

This root application controls network and hardware resources. For upstream
components, include the version from the [dependency lock](dependencies.lock.json).
The maintainer will coordinate dependency fixes through the corresponding
upstream reporting channel. Public issue templates are intended for ordinary
bugs; see [support](SUPPORT.md).
