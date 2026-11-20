#!/usr/bin/env bash
# Makes a browser/PC certificate for mutual TLS and packs it as a password-protected .p12
# (the format browsers and OS certificate stores import).
# usage: make_client.sh <name> [--out DIR] [--pass-env VAR]   (VAR holds the .p12 password)
source "$(dirname "$0")/lib.sh"
PASS_ENV=""
parse_args "$@"
[ ${#POS[@]} -eq 1 ] || die "usage: make_client.sh <name> [--out DIR] [--pass-env VAR]"
name="${POS[0]}"
[ -r "$PKI_OUT/ca.key" ] || die "no CA in $PKI_OUT — run make_ca first"
pki_key "$PKI_OUT/$name.key"
pki_cert_for_key "$PKI_OUT/$name.key" "$PKI_OUT/$name.crt" "$name" "$PKI_OUT/ca.crt" "$PKI_OUT/ca.key" 365 client
# 3DES/SHA1 protection: the one every Windows, macOS and Firefox version can import.
pass=(-passout stdin); [ -n "$PASS_ENV" ] && pass=(-passout "env:$PASS_ENV")
[ -z "$PASS_ENV" ] && echo "Enter the export password for $name.p12:"
openssl pkcs12 -export -inkey "$PKI_OUT/$name.key" -in "$PKI_OUT/$name.crt" -certfile "$PKI_OUT/ca.crt" \
    -name "$name" -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1 \
    "${pass[@]}" -out "$PKI_OUT/$name.p12"
echo "wrote $PKI_OUT/$name.key (secret), $PKI_OUT/$name.crt, $PKI_OUT/$name.p12 (import into the browser/OS)"
