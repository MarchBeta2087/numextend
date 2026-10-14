# Security Policy

## Supported Versions

Numextend follows [Semantic Versioning](https://semver.org/). Security fixes
are prepared against the latest release line (`1.x`) and land on `dev` first,
then `main`.

| Version | Supported          |
| ------- | ------------------ |
| 1.5.x   | :white_check_mark: |
| 1.4.x   | :white_check_mark: |
| < 1.4   | :x:                |

## Reporting a Vulnerability

Please **do not** open a public GitHub issue for security-sensitive reports.

Preferred channel: use GitHub's [private vulnerability reporting][advisory]
(Security → Advisories → "Report a vulnerability") on this repository.

Fallback contact: **208674917@qq.com**.

Please include, where possible:

- a description of the issue and its security impact;
- the affected version / commit (`git log -1`);
- a minimal reproduction (input values, rounding context, call sequence);
- the platform, compiler and build type (`-O2`, sanitizer-enabled, 32-bit,
  big-endian, …);
- any suggested fix or mitigation.

[advisory]: https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability

## What Qualifies

Because Numextend is a numerical library, "security" covers both classic
memory-safety defects and correctness defects that callers may rely on:

- memory-safety issues (out-of-bounds access, use-after-free, double free,
  uninitialized reads, integer overflow leading to undersized allocations);
- allocation-size / integer-overflow bugs reachable from attacker-controlled
  input (e.g. very long decimal strings or extreme precision contexts);
- deviations from the documented correctly-rounded results or documented
  special-value / error semantics that a caller could exploit;
- denial-of-service via pathological complexity or unbounded resource use on
  attacker-supplied inputs.

## Disclosure Process

1. We acknowledge receipt within **5 business days**.
2. We investigate, confirm the impact and prepare a fix on `dev`.
3. We coordinate a disclosure date with the reporter (default: within
   **90 days** of the initial report, or sooner once a release is available).
4. We credit reporters in the release notes unless they prefer to remain
   anonymous.

## Hardening Notes

- The library is pure C99 with **no external dependencies** (C standard
  library only).
- CI runs AddressSanitizer/UBSan (Linux, macOS), MSVC ASan (Windows),
  32-bit and big-endian builds, and an OOM-injection suite
  (`tests/test_oom.c`) that verifies strong exception safety on allocation
  failure.
- Thread-safety is the caller's responsibility; contexts are not shared.
