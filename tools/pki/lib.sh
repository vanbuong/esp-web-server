# Shared helpers for the PKI scripts. Works with OpenSSL 1.1.1 (Git for Windows) and 3.x.
# Sourced, not run. Every key is ECDSA P-256 unless a caller asks otherwise.
set -euo pipefail
# Git Bash would rewrite "/CN=..." into a Windows path; turn that conversion off.
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'

PKI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKI_OUT="${PKI_OUT:-$PKI_DIR/out}"

die() { echo "error: $*" >&2; exit 1; }
# Temp files live next to the output: Git for Windows' openssl.exe is a native Windows program
# and cannot open MSYS paths like /tmp.
tmpf() { mktemp "$PKI_OUT/.tmp.XXXXXX"; }
command -v openssl >/dev/null || die "openssl not found"

# Parses "--out DIR" (also -Out/-out from the PowerShell wrappers); remaining args in POS[].
parse_args() {
    POS=()
    while [ $# -gt 0 ]; do
        case "$1" in
            --out|-Out|-out) PKI_OUT="$2"; shift 2 ;;
            --pass-env) PASS_ENV="$2"; shift 2 ;;
            *) POS+=("$1"); shift ;;
        esac
    done
    mkdir -p "$PKI_OUT"
}

# pki_key <file> [rsa]  — new private key (P-256 PKCS#8, or RSA-2048), readable by owner only.
pki_key() {
    if [ "${2:-}" = rsa ]; then
        openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$1" 2>/dev/null
    else
        openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$1" 2>/dev/null
    fi
    chmod 600 "$1"
}

# pki_ext <file> <kind> [hostname]  — extension config. kind: ca | int | server | client
# Why each extension: browsers only trust names in subjectAltName (CN is ignored); the
# extendedKeyUsage pins a cert to one role, so a client cert can't impersonate the device.
pki_ext() {
    {
        printf '[req]\ndistinguished_name=dn\n[dn]\n[ext]\n'
        case "$2" in
            ca)  printf 'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\n' ;;
            int) printf 'basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid\n' ;;
            server) printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:%s.local\nauthorityKeyIdentifier=keyid\n' "$3" ;;
            client) printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=clientAuth\nauthorityKeyIdentifier=keyid\n' ;;
            *) die "unknown extension kind $2" ;;
        esac
    } > "$1"
}

# pki_self_ca <key> <crt> <cn> <days>  — self-signed root CA.
pki_self_ca() {
    local ext; ext="$(tmpf)"; pki_ext "$ext" ca
    openssl req -x509 -new -key "$1" -sha256 -days "$4" -subj "/CN=$3" \
        -config "$ext" -extensions ext -out "$2"
    rm -f "$ext"
}

# pki_issue <csr> <out.crt> <ca.crt> <ca.key> <days> <kind> [hostname]
# Signs a CSR; the extensions come from <kind>/<hostname>, NOT from the CSR — the CA decides names.
pki_issue() {
    local ext; ext="$(tmpf)"; pki_ext "$ext" "$6" "${7:-}"
    openssl x509 -req -in "$1" -CA "$3" -CAkey "$4" -set_serial "0x$(openssl rand -hex 16)" \
        -days "$5" -sha256 -extfile "$ext" -extensions ext -out "$2" 2>/dev/null
    rm -f "$ext"
}

# pki_cert_for_key <key> <out.crt> <cn> <ca.crt> <ca.key> <days> <kind> [hostname]
pki_cert_for_key() {
    local csr; csr="$(tmpf)"
    openssl req -new -key "$1" -subj "/CN=$3" -out "$csr"
    pki_issue "$csr" "$2" "$4" "$5" "$6" "$7" "${8:-}"
    rm -f "$csr"
}
