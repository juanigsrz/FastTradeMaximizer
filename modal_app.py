"""
Modal service for FastTradeMaximizer.

The C++ solver is compiled into the container image at build time; the running
function just pipes an uploaded wants file through it. Exposes a tiny HTTP API:

    GET  /        -> health check
    POST /solve   -> body = raw wants.txt text; returns {ok, ms, output, log}

Deploy (run from the repo root so the source files are found):

    pip install modal
    modal token new
    modal deploy modal_app.py

Then copy the printed https://<workspace>--fasttrademaximizer-web.modal.run URL
into docs/index.html (the MODAL_URL field, or paste it in the page at runtime).

Local dev with autoreload:  modal serve modal_app.py
"""
import gzip
import hashlib
import subprocess
import time

import modal

# ---- limits ----
MAX_INPUT_BYTES = 32 * 1024 * 1024   # 32 MB; the largest bundled testcase is ~11 MB
SOLVE_TIMEOUT_S = 60                 # hard cap on a single solve

# ?input=<url> may only fetch from these hosts (on top of the public-IP SSRF guard).
# Set to None to allow any public host; set to an empty set to disable URL fetching.
ALLOWED_INPUT_HOSTS = {"bgg.activityclub.org", "juanigsrz.github.io"}

# Result cache: serve a previously computed result for the same input within this
# window. Content-addressed (keyed on the input bytes), so a changed file misses.
# Bump CACHE_NS when the solver changes so old entries are ignored.
CACHE_TTL_S = 7 * 24 * 3600
CACHE_NS = "ftm-0.5"

FTM_BIN = "/app/ftm"

# Build the image once: g++ + the C++ sources, compiled at image build time so
# requests don't pay any compilation cost. Sources are copied (copy=True) so the
# following run_commands can see them.
image = (
    modal.Image.debian_slim(python_version="3.12")
    .apt_install("g++")
    .pip_install("fastapi[standard]", "httpx")
    .add_local_file("main.cpp", "/src/main.cpp", copy=True)
    .add_local_file("network_simplex.hpp", "/src/network_simplex.hpp", copy=True)
    .add_local_file("utils.hpp", "/src/utils.hpp", copy=True)
    .add_local_file("md5.hpp", "/src/md5.hpp", copy=True)
    .run_commands(
        "mkdir -p /app",
        f"g++ -std=c++17 -O3 -I/src /src/main.cpp -o {FTM_BIN}",
    )
)

app = modal.App("fasttrademaximizer")

# Managed key-value store: persists across invocations, costs only storage (no idle
# compute), and the function still scales to zero.
result_cache = modal.Dict.from_name("ftm-result-cache", create_if_missing=True)


@app.function(image=image, timeout=SOLVE_TIMEOUT_S + 60, cpu=2.0, memory=2048)
@modal.asgi_app()
def web():
    import ipaddress
    import socket
    from urllib.parse import urlparse

    import httpx
    from fastapi import FastAPI, HTTPException, Request
    from fastapi.middleware.cors import CORSMiddleware
    from fastapi.responses import JSONResponse, PlainTextResponse

    api = FastAPI(title="FastTradeMaximizer")

    # Served from a static GitHub Pages site -> browser calls are cross-origin.
    # Tighten allow_origins to your Pages domain (e.g. ["https://USER.github.io"])
    # if you want to restrict who can call it.
    api.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_methods=["GET", "POST", "OPTIONS"],
        allow_headers=["*"],
    )

    def assert_public_url(url: str):
        # SSRF guard: http/https only, and the host must resolve to public IPs
        # (reject loopback / private / link-local / cloud-metadata addresses).
        p = urlparse(url)
        if p.scheme not in ("http", "https"):
            raise HTTPException(400, "input URL must be http or https.")
        if not p.hostname:
            raise HTTPException(400, "input URL has no host.")
        if ALLOWED_INPUT_HOSTS is not None and p.hostname.lower() not in ALLOWED_INPUT_HOSTS:
            raise HTTPException(400, f"host not allowed: {p.hostname}")
        port = p.port or (443 if p.scheme == "https" else 80)
        try:
            infos = socket.getaddrinfo(p.hostname, port, proto=socket.IPPROTO_TCP)
        except OSError:
            raise HTTPException(400, f"cannot resolve host: {p.hostname}")
        for *_, sockaddr in infos:
            ip = ipaddress.ip_address(sockaddr[0])
            if not ip.is_global or ip.is_multicast:
                raise HTTPException(400, f"refusing non-public address: {ip}")

    async def fetch_remote(url: str) -> bytes:
        # Re-validate every hop so a redirect can't bounce us to an internal host.
        # verify=False: OLWLG (bgg.activityclub.org) serves an incomplete TLS chain --
        # it omits the Let's Encrypt intermediate, so strict verification fails. The
        # input is public, non-sensitive data and the host is allowlisted, so we accept
        # the unverified fetch here.
        try:
            async with httpx.AsyncClient(timeout=30.0, verify=False) as client:
                for _ in range(4):
                    assert_public_url(url)
                    async with client.stream(
                        "GET", url, headers={"User-Agent": "FastTradeMaximizer"}
                    ) as resp:
                        if resp.is_redirect:
                            loc = resp.headers.get("location")
                            if not loc:
                                raise HTTPException(502, "redirect without Location header.")
                            url = str(httpx.URL(url).join(loc))
                            continue
                        if resp.status_code != 200:
                            raise HTTPException(502, f"fetch failed: HTTP {resp.status_code}.")
                        buf = bytearray()
                        async for chunk in resp.aiter_bytes():
                            buf += chunk
                            if len(buf) > MAX_INPUT_BYTES:
                                raise HTTPException(413, f"remote input too large (> {MAX_INPUT_BYTES} bytes).")
                        return bytes(buf)
        except httpx.HTTPError as e:
            raise HTTPException(502, f"could not fetch input URL: {e}")
        raise HTTPException(502, "too many redirects.")

    @api.get("/")
    def health():
        return {"ok": True, "service": "FastTradeMaximizer"}

    @api.api_route("/solve", methods=["GET", "POST"])
    async def solve(request: Request, input: str | None = None, raw: bool = False):
        if input:
            body = await fetch_remote(input)
        else:
            body = await request.body()
        if not body:
            raise HTTPException(400, "No input. Send the wants file as the body, or pass ?input=<url>.")
        if len(body) > MAX_INPUT_BYTES:
            raise HTTPException(413, f"Input too large ({len(body)} bytes > {MAX_INPUT_BYTES}).")

        # Content-addressed cache: identical input within the TTL skips the solve.
        cache_key = hashlib.sha256(CACHE_NS.encode() + body).hexdigest()
        now = time.time()
        hit = result_cache.get(cache_key)
        if hit and now - hit["ts"] < CACHE_TTL_S:
            out = gzip.decompress(hit["out"]).decode("utf-8", "replace")
            if raw:
                return PlainTextResponse(out)
            return JSONResponse({"ok": True, "ms": hit["ms"], "output": out, "log": "", "cached": True})

        started = time.time()
        try:
            proc = subprocess.run(
                [FTM_BIN],
                input=body,
                capture_output=True,
                timeout=SOLVE_TIMEOUT_S,
            )
        except subprocess.TimeoutExpired:
            raise HTTPException(504, f"Solver exceeded the {SOLVE_TIMEOUT_S}s limit.")
        ms = int((time.time() - started) * 1000)

        out = proc.stdout.decode("utf-8", "replace")
        log = proc.stderr.decode("utf-8", "replace")

        if proc.returncode != 0:
            # The solver aborts (assert) on malformed input -> non-zero exit,
            # usually with empty stdout. Surface the tail of stderr to help debug.
            raise HTTPException(
                status_code=422,
                detail={
                    "error": "Solver failed -- likely malformed input.",
                    "returncode": proc.returncode,
                    "log": log[-4000:],
                },
            )

        result_cache[cache_key] = {"ts": now, "ms": ms, "out": gzip.compress(out.encode("utf-8"))}

        if raw:  # ?raw=1 -> just the solver output, plain text
            return PlainTextResponse(out)
        return JSONResponse({"ok": True, "ms": ms, "output": out, "log": log, "cached": False})

    return api
