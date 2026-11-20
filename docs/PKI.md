# Certificates (PKI) for the gateway

The gateway serves HTTPS with a certificate from **your own certificate authority (CA)**. In mutual-TLS
mode (the default) browsers must also show a certificate from that CA before the device talks to them.

## Who owns what

| Item | Made where | Lives where | Why |
|---|---|---|---|
| **Root CA** key `ca.key` | your PC, once (`make_ca`) | **only your PC**, ideally offline/backed up — never on a device | whoever has it can make certificates every gateway and browser trusts |
| Root CA cert `ca.crt` | with the key | every gateway (uploaded at setup) and every browser/OS trust store | lets each side check the other's certificate |
| **Gateway key** | the gateway itself (CSR path, default) — or your PC (upload path) | that gateway only | proves "I am gatewayXXXX" |
| Gateway cert `gatewayXXXX.crt` | signed by your CA (`sign_csr` / `make_device`) | that gateway | binds the key to the name `gatewayXXXX.local` |
| **Client** key + cert, `name.p12` | your PC (`make_client`) | one browser / PC each | in mutual-TLS mode, the device only talks to holders of these |

So yes, you need keys and certificates for the CA, the server and (for mutual TLS) each client, but
each one has a different owner, and the CA key never travels.

## Choices and why

- **ECDSA P-256 keys**: a TLS handshake with them is several times faster on an ESP32 than RSA-2048,
  and the keys/certificates are small. (Uploaded RSA-2048 keys are still accepted.)
- **The name goes in subjectAltName** (`DNS:gatewayXXXX.local`): browsers ignore the CN for names.
  `sign_csr` writes the SAN from the hostname you type, not from the request — the CA decides names.
- **Extended key usage**: gateway certs are `serverAuth`, client certs `clientAuth`, so a stolen
  browser certificate can't be used to impersonate a gateway (the gateway rejects non-server certs).
- **Validity**: CA 10 years, gateway 2 years, client 1 year. The device doesn't check dates (it may have
  no clock); browsers do, so renew gateway certs before they expire (admin API, plan 2).
- **CSR path (default)**: the gateway generates its key and gives you a certificate request; the key
  never leaves the chip. The **upload path** (key made on the PC) exists for convenience and labs.
- **Product step: intermediate CA.** Keep the root offline and sign day-to-day with an intermediate.
  The gateway accepts `cert` = device cert + intermediate, `ca` = root. Not scripted yet.
- **Revocation**: not supported. To withdraw a lost client certificate today you make a new CA and
  re-provision. (A CN allow-list is a possible later feature.)

## Commands

PowerShell (uses Git for Windows' OpenSSL; no WSL needed) — or run the `.sh` files in bash directly.
Everything goes to `tools/pki/out/` (git-ignored) unless you pass `-Out <dir>`.

```powershell
./tools/pki/make_ca.ps1                              # once: ca.key + ca.crt
./tools/pki/sign_csr.ps1 gatewayE46C.csr gatewayE46C # CSR path: sign the request downloaded from the device
./tools/pki/make_device.ps1 gatewayE46C              # upload path: key + cert made here
./tools/pki/make_client.ps1 laptop1                  # asks for a password, writes laptop1.p12
```

## Trusting the CA and installing a client certificate

- **Windows (Chrome, Edge)**: double-click `ca.crt` → Install Certificate → Current User → *Trusted Root
  Certification Authorities*. Double-click `laptop1.p12` → Current User → enter the password → automatic store.
- **macOS (Safari, Chrome)**: open `ca.crt` in Keychain Access → set *Always Trust*. Double-click `laptop1.p12`.
- **Firefox** (own store): Settings → Privacy & Security → Certificates → View Certificates →
  *Authorities* → Import `ca.crt` (trust for websites); *Your Certificates* → Import `laptop1.p12`.
- In mutual-TLS mode the browser asks which certificate to use when you open `https://gatewayXXXX.local`.
  Without one, it shows a connection error and the device never serves a page.
