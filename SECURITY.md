# Security Policy

## Supported Versions

Security updates are provided for the latest release of each actively maintained major version.

Security updates are not backported to older minor or patch releases. To receive security patches, update to the latest release of a supported major version.

## Reporting a Vulnerability

Do not report security vulnerabilities through public issues, pull requests, or discussions.

Report vulnerabilities through one of the following private channels:

1. **GitHub Private Vulnerability Reporting** (*preferred*): Submit an advisory through the [Security Advisories](https://github.com/dio-az/nginx-vod-module/security/advisories/new) page.
2. **Email**: Send the report to [hi@dio-az.dev](mailto:hi@dio-az.dev).

### What to Include

To help us investigate quickly, include:

- A description of the vulnerability and its potential impact.
- Affected directives, format (HLS, DASH), and working mode (`local`, `remote`, or `mapped`).
- Reproduction steps or a minimal configuration and media sample.
- Proof-of-concept code, crash logs, or sanitizer output.

### Response Process

- **Acknowledgment**: We acknowledge vulnerability reports as soon as possible.
- **Investigation**: We validate the report, assess its severity, and determine affected versions.
- **Update and Disclosure**: We prepare security updates privately and publish them with a coordinated security advisory.
