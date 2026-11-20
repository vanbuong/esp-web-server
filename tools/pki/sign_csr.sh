#!/usr/bin/env bash
# Signs a gateway's certificate request (CSR path: the private key never left the device).
# usage: sign_csr.sh <request.csr> <hostname> [--out DIR]     e.g. sign_csr.sh gw.csr gatewayE46C
source "$(dirname "$0")/lib.sh"
parse_args "$@"
[ ${#POS[@]} -eq 2 ] || die "usage: sign_csr.sh <request.csr> <hostname> [--out DIR]"
csr="${POS[0]}"; host="${POS[1]}"
[ -r "$PKI_OUT/ca.key" ] || die "no CA in $PKI_OUT — run make_ca first"
openssl req -in "$csr" -noout -verify 2>/dev/null || die "$csr is not a valid, self-signed CSR"
pki_issue "$csr" "$PKI_OUT/$host.crt" "$PKI_OUT/ca.crt" "$PKI_OUT/ca.key" 730 server "$host"
echo "wrote $PKI_OUT/$host.crt (valid for $host.local)"
