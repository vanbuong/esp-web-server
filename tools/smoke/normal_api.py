"""Smoke test for a provisioned gateway in normal (HTTPS) mode. Python 3 standard library only.

Run from a PC on the SAME network as the gateway (not a guest Wi-Fi with client isolation):
  python tools/smoke/normal_api.py --host gatewayE46C --ca tools/pki/out/ca.crt \\
      --cert tools/pki/out/laptop1.crt --key tools/pki/out/laptop1.key --password Learning-123
Mutual-TLS builds need --cert/--key. Optional --bad-cert/--bad-key: a client cert from another CA.
Exit code = number of failed checks.
"""
import argparse
import http.client
import json
import socket
import ssl
import sys

FAILS = 0


def check(name, ok, detail=""):
    global FAILS
    print(("PASS  " if ok else "FAIL  ") + name + ("" if ok else f"  {detail}"))
    FAILS += 0 if ok else 1


def tls_context(ca, cert=None, key=None):
    ctx = ssl.create_default_context(cafile=ca)   # verifies the gateway cert + its name
    if cert:
        ctx.load_cert_chain(cert, key)
    return ctx


def request(args, ctx, method, path, body=None, cookie=None):
    """One HTTPS request → (status, headers, parsed JSON or text). Raises on TLS failure."""
    conn = http.client.HTTPSConnection(args.addr or args.fqdn, 443, context=ctx, timeout=20)
    if args.addr:   # connect by IP but still check the certificate name
        sock = socket.create_connection((args.addr, 443), timeout=20)
        conn.sock = ctx.wrap_socket(sock, server_hostname=args.fqdn)
    headers = {"Content-Type": "application/json"} if body is not None else {}
    if cookie:
        headers["Cookie"] = cookie
    conn.request(method, path, None if body is None else json.dumps(body), headers)
    r = conn.getresponse()
    raw = r.read().decode("utf-8", "replace")
    conn.close()
    try:
        data = json.loads(raw)
    except ValueError:
        data = raw
    return r.status, {k.lower(): v for k, v in r.getheaders()}, data


def handshake_fails(args, ctx):
    try:
        request(args, ctx, "GET", "/api/status")
        return False
    except (ssl.SSLError, ConnectionError, OSError):
        return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True, help="gatewayXXXX (without .local)")
    ap.add_argument("--addr", help="IP address, if .local names don't resolve on this PC")
    ap.add_argument("--ca", required=True)
    ap.add_argument("--cert")
    ap.add_argument("--key")
    ap.add_argument("--bad-cert")
    ap.add_argument("--bad-key")
    ap.add_argument("--password", required=True)
    args = ap.parse_args()
    args.fqdn = args.host + ".local"

    good = tls_context(args.ca, args.cert, args.key)
    no_cert = tls_context(args.ca)

    st, _, s = request(args, good, "GET", "/api/status")
    check("status over HTTPS", st == 200 and s.get("mode") == "normal", f"{st} {s}")
    mutual = s.get("tls_mode") == "mutual"
    print(f"      tls_mode={s.get('tls_mode')} client_cn={s.get('client_cn')}")
    if mutual:
        check("mutual: handshake without client cert fails", handshake_fails(args, no_cert))
        check("mutual: client_cn reported", bool(s.get("client_cn")), str(s))
        if args.bad_cert:
            check("mutual: client cert from another CA fails",
                  handshake_fails(args, tls_context(args.ca, args.bad_cert, args.bad_key)))
    else:
        check("server-auth: works without client cert", not handshake_fails(args, no_cert))

    st, _, _ = request(args, good, "POST", "/api/login", {"user": "admin", "password": "wrong-pass"})
    check("wrong password -> 401", st == 401, str(st))
    st, hd, body = request(args, good, "POST", "/api/login", {"user": "admin", "password": args.password})
    cookie_hdr = hd.get("set-cookie", "")
    check("login -> 200", st == 200, f"{st} {body}")
    check("session cookie is Secure + HttpOnly", "Secure" in cookie_hdr and "HttpOnly" in cookie_hdr, cookie_hdr)
    cookie = cookie_hdr.split(";")[0]

    st, _, s = request(args, good, "GET", "/api/status", cookie=cookie)
    check("status details when logged in", st == 200 and "heap_free" in s and "cert" in s, str(s))
    heap0 = s.get("heap_free", 0)
    print(f"      ip={s.get('ip')} rssi={s.get('rssi')} heap_free={heap0} heap_min={s.get('heap_min')} "
          f"cert_until={s.get('cert', {}).get('not_after')} time_valid={s.get('time_valid')}")

    # Plain HTTP on port 80 only redirects.
    try:
        c = http.client.HTTPConnection(args.addr or args.fqdn, 80, timeout=10)
        c.request("GET", "/api/status")
        r = c.getresponse()
        loc = r.getheader("Location", "")
        check("http:// -> 301 to https://", r.status == 301 and loc == f"https://{args.fqdn}/api/status",
              f"{r.status} {loc}")
        c.close()
    except OSError as e:
        check("http:// -> 301 to https://", False, repr(e))

    # Failed handshakes must not leak memory or wedge the server.
    if mutual:
        for _ in range(20):
            handshake_fails(args, no_cert)
        st, _, s = request(args, good, "GET", "/api/status", cookie=cookie)
        heap1 = s.get("heap_free", 0)
        check("after 20 failed handshakes: still serving", st == 200, str(st))
        check("after 20 failed handshakes: heap within 4 KB", abs(heap1 - heap0) <= 4096,
              f"before {heap0} after {heap1}")

    st, _, _ = request(args, good, "POST", "/api/logout", {}, cookie=cookie)
    check("logout -> 200", st == 200, str(st))

    print(f"\n{FAILS} check(s) failed")
    return FAILS


if __name__ == "__main__":
    sys.exit(main())
