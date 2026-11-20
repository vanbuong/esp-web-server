"""Smoke test for a provisioned gateway in normal (HTTPS) mode. Python 3 standard library only.

Run from a PC on the SAME network as the gateway (not a guest Wi-Fi with client isolation):
  python tools/smoke/normal_api.py --host gatewayE46C --ca tools/pki/out/ca.crt \\
      --cert tools/pki/out/laptop1.crt --key tools/pki/out/laptop1.key --password Learning-123
Mutual-TLS builds need --cert/--key. Optional --bad-cert/--bad-key: a client cert from another CA.
--renew --pki DIR: password change round trip + certificate renewal via a new device CSR, signed
  with DIR/ca.key by tools/pki/sign_csr.sh; reboots the gateway.
--factory-reset: erases the gateway's settings (destructive; run it last, on its own).
Exit code = number of failed checks.
"""
import argparse
import hashlib
import http.client
import json
import os
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import time

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


def server_fingerprint(args, ctx):
    sock = socket.create_connection((args.addr or args.fqdn, 443), timeout=20)
    with ctx.wrap_socket(sock, server_hostname=args.fqdn) as t:
        der = t.getpeercert(binary_form=True)
    return hashlib.sha256(der).hexdigest()[:16], ssl.DER_cert_to_PEM_cert(der)


def sign_csr(csr_pem, host, pki):
    """Signs with tools/pki/sign_csr.sh in a temp copy of the CA (keeps the user's folder clean)."""
    tmp = tempfile.mkdtemp()
    try:
        for f in ("ca.crt", "ca.key"):
            shutil.copy(os.path.join(pki, f), tmp)
        csr = os.path.join(tmp, "dev.csr")
        with open(csr, "w") as f:
            f.write(csr_pem)
        script = os.path.join(os.path.dirname(__file__), "..", "pki", "sign_csr.sh")
        bash = "C:/Program Files/Git/bin/bash.exe" if os.name == "nt" else "bash"
        subprocess.run([bash, script.replace("\\", "/"), csr, host, "--out", tmp], check=True,
                       capture_output=True)
        with open(os.path.join(tmp, host + ".crt")) as f:
            return f.read()
    finally:
        shutil.rmtree(tmp)   # held a copy of the CA key


def wait_back(args, ctx, seconds=60):
    end = time.time() + seconds
    time.sleep(5)
    while time.time() < end:
        try:
            if request(args, ctx, "GET", "/api/status")[0] == 200:
                return True
        except (ssl.SSLError, OSError):
            pass
        time.sleep(2)
    return False


def renew(args, good, cookie):
    pw2 = args.password + "-x"
    st, _, b = request(args, good, "POST", "/api/admin/password",
                       {"current_password": "wrong-pass", "new_password": pw2}, cookie)
    check("admin password: wrong current -> 403", st == 403, f"{st} {b}")
    st, hd, b = request(args, good, "POST", "/api/admin/password",
                        {"current_password": args.password, "new_password": pw2}, cookie)
    cookie = hd.get("set-cookie", "").split(";")[0] or cookie
    check("admin password: change -> 200 + new cookie", st == 200, f"{st} {b}")
    st, hd, b = request(args, good, "POST", "/api/admin/password",
                        {"current_password": pw2, "new_password": args.password}, cookie)
    cookie = hd.get("set-cookie", "").split(";")[0] or cookie
    check("admin password: change back -> 200", st == 200, f"{st} {b}")

    fp_old, pem_old = server_fingerprint(args, good)
    st, _, b = request(args, good, "POST", "/api/admin/tls/csr", {}, cookie)
    check("renew: new CSR -> 200", st == 200 and "BEGIN CERTIFICATE REQUEST" in b.get("csr", ""), f"{st}")
    check("renew: old certificate still served", server_fingerprint(args, good)[0] == fp_old)
    st, _, t = request(args, good, "GET", "/api/admin/tls", cookie=cookie)
    check("renew: pending_csr reported", st == 200 and t.get("pending_csr") is True, str(t))

    ca = open(args.ca).read()
    st, _, r = request(args, good, "POST", "/api/admin/tls/cert", {"cert": pem_old, "ca": ca}, cookie)
    check("renew: old cert with new key -> 400 key_mismatch", st == 400 and r.get("error") == "key_mismatch", f"{st} {r}")
    new_pem = sign_csr(b["csr"], args.host, args.pki)
    st, _, r = request(args, good, "POST", "/api/admin/tls/cert", {"cert": new_pem, "ca": ca}, cookie)
    check("renew: signed new cert -> 200 reboot_required", st == 200 and r.get("reboot_required") is True, f"{st} {r}")

    st, _, _ = request(args, good, "POST", "/api/admin/reboot", {}, cookie)
    check("reboot -> 200", st == 200, str(st))
    check("gateway back after reboot", wait_back(args, good))
    check("renew: new certificate served after reboot", server_fingerprint(args, good)[0] != fp_old)


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
    ap.add_argument("--renew", action="store_true")
    ap.add_argument("--pki", help="folder with ca.crt + ca.key (for --renew)")
    ap.add_argument("--factory-reset", action="store_true")
    args = ap.parse_args()
    if args.renew and not args.pki:
        ap.error("--renew needs --pki")
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

    if args.factory_reset:
        st, _, b = request(args, good, "POST", "/api/admin/factory-reset", {"password": "wrong-pass"}, cookie)
        check("factory reset: wrong password -> 403", st == 403, f"{st} {b}")
        st, _, b = request(args, good, "POST", "/api/admin/factory-reset", {"password": args.password}, cookie)
        check("factory reset -> 200 (gateway reboots into setup mode)", st == 200, f"{st} {b}")
        print(f"\n{FAILS} check(s) failed")
        return FAILS

    if args.renew:
        renew(args, good, cookie)
        st, _, _ = request(args, good, "POST", "/api/login", {"user": "admin", "password": args.password})
        check("login after reboot with original password", st == 200, str(st))
    else:
        st, _, _ = request(args, good, "POST", "/api/logout", {}, cookie=cookie)
        check("logout -> 200", st == 200, str(st))

    print(f"\n{FAILS} check(s) failed")
    return FAILS


if __name__ == "__main__":
    sys.exit(main())
