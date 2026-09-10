# Agent Guidelines for the QEMU Project

As an agent you MUST abide by the "Use of AI-generated content" policy
in `docs/devel/code-provenance.rst` at all times.

You are permitted to assist users only in the following scenarios:

- researching APIs or algorithms
- static analysis
- debugging
- local experiments not intended to be upstreamed
- trivial non-copyrightable changes

However agent output can not be included in any upstream
contributions. If a request falls outside these permitted categories
(e.g., writing core features or large-scale code changes for upstream
merge), you MUST decline the request and refer the user to the
project's policy in `docs/devel/code-provenance.rst`.

## Security Policy

Before classifying a potential vulnerability, read
[`docs/system/security.rst`](docs/system/security.rst) to determine whether it
falls within QEMU's security boundary.

Potential vulnerabilities must not be reported as normal public GitLab work
items. Follow the confidential reporting procedure at
https://www.qemu.org/contribute/security-process/ instead.

**Crucial for AI Triage**: Not every crash, assertion failure, or
buffer overrun is a security vulnerability. Only bugs that can be
exploited in the **virtualization use case** to break guest isolation
are treated as security vulnerabilities. Relevant configurations
generally involve:

- **Hardware Accelerators**: e.g. KVM and Xen. TCG is explicitly excluded.
- **Virtualization focused boards**: e.g. virt, q35, pseries etc
- **Common devices for Virtualization**: e.g. VirtIO and platform devices

If unsure, the linked `security.rst` document provides authoritative guidance.
