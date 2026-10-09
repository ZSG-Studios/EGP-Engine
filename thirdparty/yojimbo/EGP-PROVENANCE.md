# EGP vendor provenance

EGP-UPSTREAM.json pins the retained Yojimbo sources and the existing netcode
patch. Native xmake recipes replace the upstream CMake build definitions.
Their original raw and normalized digests remain in excluded_upstream_files;
the active source and normalization maps contain only retained files.
This intentional build-system omission does not change or recertify vendor code.
