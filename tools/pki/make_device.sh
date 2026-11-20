#!/usr/bin/env bash
# Upload path: makes a gateway key + certificate on this PC (the key then has to be uploaded,
# so prefer the device's own CSR — see sign_csr.sh).
# usage: make_device.sh <hostname> [--out DIR]
source "$(dirname "$0")/lib.sh"
parse_args "$@"
[ ${#POS[@]} -eq 1 ] || die "usage: make_device.sh <hostname> [--out DIR]"
host="${POS[0]}"
[ -r "$PKI_OUT/ca.key" ] || die "no CA in $PKI_OUT — run make_ca first"
pki_key "$PKI_OUT/$host.key"
pki_cert_for_key "$PKI_OUT/$host.key" "$PKI_OUT/$host.crt" "$host" "$PKI_OUT/ca.crt" "$PKI_OUT/ca.key" 730 server "$host"
echo "wrote $PKI_OUT/$host.key (secret)"
echo "wrote $PKI_OUT/$host.crt (valid for $host.local)"
