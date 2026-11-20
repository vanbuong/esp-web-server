#!/usr/bin/env bash
# Creates your private root CA: ca.key (keep it offline — it can sign anything) and ca.crt
# (goes to every gateway and into every browser/OS trust store).
# usage: make_ca.sh [--out DIR]
source "$(dirname "$0")/lib.sh"
parse_args "$@"
[ -e "$PKI_OUT/ca.key" ] && die "$PKI_OUT/ca.key exists — refusing to overwrite your CA"
pki_key "$PKI_OUT/ca.key"
pki_self_ca "$PKI_OUT/ca.key" "$PKI_OUT/ca.crt" "Gateway Root CA" 3650
echo "wrote $PKI_OUT/ca.key (keep secret, offline)"
echo "wrote $PKI_OUT/ca.crt"
