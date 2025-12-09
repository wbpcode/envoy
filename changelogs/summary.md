**Summary of changes**:

* Security updates:

  Resolve dependency CVEs:
  - CVE-2025-0725: curl
  - CVE-2024-7246: gRPC
  - CVE-2024-11407: gRPC
  - CVE-2025-27817: kafka
  - CVE-2025-27818: kafka
  - CVE-2024-51745: wasmtime
  - CVE-2025-53901: wasmtime

  - c-ares/CVE-2025-0913:
      Use after free can crash Envoy due to malfunctioning or compromised DNS.
