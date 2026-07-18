# Security Policy

## Supported Versions

ent is in the **0.x / Preview** stage. The latest stable release and the latest
commit on `master` receive security fixes. Older releases and commits do not
receive backports.

| Version | Supported |
|---|---|
| latest stable release | Yes |
| latest `master` | Yes |
| older releases / commits | No |

## Reporting a Vulnerability

**Do not open a public GitHub issue for a suspected vulnerability.** Public
disclosure before a fix is available can put users at risk.

### Preferred Channel

Use [GitHub Private Security Advisories](https://github.com/if16888/ent/security/advisories/new)
to report a vulnerability confidentially.

Include:

- A brief description of the vulnerability.
- Steps to reproduce or a minimal proof of concept.
- The affected component and version or commit hash.
- The expected impact.
- Any suggested remediation, when available.

Do not include credentials, private keys, production data, or unrelated
personal information in the report.

If the advisory form is temporarily unavailable, do not send vulnerability
details through a public issue or discussion. Retry the private advisory form
later.

### Response Commitment

This project is maintained on a best-effort basis and has no guaranteed SLA.
The maintainer will make a reasonable effort to:

1. Acknowledge receipt within **7 business days**.
2. Provide an initial triage assessment within **14 business days**.
3. Coordinate a fix or mitigation before public disclosure when the report is
   validated.

### Scope

Reports are in scope for:

- Memory-safety issues in ent itself, including use-after-free and
  out-of-bounds access.
- Concurrency defects that can cause corruption, invalid resource ownership,
  or a security boundary failure.
- Input-validation defects in public APIs.
- Build, CI, or release configuration defects that could introduce malicious
  or unverified artifacts.

The following are normally out of scope:

- Vulnerabilities solely in third-party dependencies. Report these to the
  relevant upstream project, while also notifying ent when the dependency is
  shipped or enabled by ent.
- Issues that require an already-compromised administrator or root account and
  do not create an additional security boundary violation.
- Theoretical concerns without a reproducible impact path.
