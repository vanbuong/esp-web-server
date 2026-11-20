#!/usr/bin/env bash
# Throwaway certificates for the host unit tests (hostname "gatewayTEST"). Regenerated on demand
# so no private key is ever committed. usage: make_test_fixtures.sh <dir>
source "$(dirname "$0")/lib.sh"
d="${1:?usage: make_test_fixtures.sh <dir>}"
mkdir -p "$d"
PKI_OUT="$d"   # tmpf() writes here
[ -s "$d/client.crt" ] && exit 0          # already generated (last file written)
H=gatewayTEST

pki_key "$d/ca.key";  pki_self_ca "$d/ca.key"  "$d/ca.crt"  "Gateway Root CA" 3650
pki_key "$d/ca2.key"; pki_self_ca "$d/ca2.key" "$d/ca2.crt" "Other CA" 3650

# Device key in both encodings: PKCS#8 (genpkey) and SEC1 "EC PRIVATE KEY".
pki_key "$d/dev_pkcs8.key"
openssl ec -in "$d/dev_pkcs8.key" -out "$d/dev.key" 2>/dev/null
pki_cert_for_key "$d/dev.key" "$d/dev.crt"            "$H"        "$d/ca.crt"  "$d/ca.key"  730 server "$H"
pki_cert_for_key "$d/dev.key" "$d/dev_other_name.crt" "gatewayOTHR" "$d/ca.crt" "$d/ca.key" 730 server gatewayOTHR
pki_cert_for_key "$d/dev.key" "$d/dev_by_ca2.crt"     "$H"        "$d/ca2.crt" "$d/ca2.key" 730 server "$H"
# Right name (SAN) but only allowed for client authentication: must fail the EKU rule.
ext="$(tmpf)"; pki_ext "$ext" client
printf 'subjectAltName=DNS:%s.local\n' "$H" >> "$ext"
csr="$(tmpf)"; openssl req -new -key "$d/dev.key" -subj "/CN=$H" -out "$csr"
openssl x509 -req -in "$csr" -CA "$d/ca.crt" -CAkey "$d/ca.key" -set_serial "0x$(openssl rand -hex 16)" \
    -days 730 -sha256 -extfile "$ext" -extensions ext -out "$d/dev_client_eku.crt" 2>/dev/null
rm -f "$ext" "$csr"

pki_key "$d/dev_rsa.key" rsa
pki_cert_for_key "$d/dev_rsa.key" "$d/dev_rsa.crt" "$H" "$d/ca.crt" "$d/ca.key" 730 server "$H"

# Chain: ca → intermediate → device.
pki_key "$d/int.key"
pki_cert_for_key "$d/int.key" "$d/int.crt" "Gateway Intermediate CA" "$d/ca.crt" "$d/ca.key" 1825 int
pki_key "$d/dev_via_int.key"
pki_cert_for_key "$d/dev_via_int.key" "$d/dev_via_int.crt" "$H" "$d/int.crt" "$d/int.key" 730 server "$H"

pki_key "$d/client.key"
pki_cert_for_key "$d/client.key" "$d/client.crt" "laptop1" "$d/ca.crt" "$d/ca.key" 365 client
echo "fixtures in $d"
