# FastTradeMaximizer API

Run the math-trade solver over HTTP. Send a wants file, get the trade loops back.

**Base URL**

```
https://juanigsrz--fasttrademaximizer-web.modal.run
```

## Endpoints

### `GET /`

Health check.

```json
{ "ok": true, "service": "FastTradeMaximizer" }
```

### `POST /solve` · `GET /solve`

Solve a math trade. Provide the wants file one of two ways:

- **Request body** (POST): the raw wants file as plain text — not multipart, not JSON,
  just the file contents. Content-Type is ignored; the body is read verbatim.
- **`?input=<url>`** (GET or POST): a public `http`/`https` URL the service fetches the
  wants file from, instead of the body. Handy for OLWLG official-wants links:

  ```
  GET /solve?input=https://bgg.activityclub.org/olwlg/377645-officialwants.txt
  ```

  The URL host must be on the allowlist — currently `bgg.activityclub.org` and
  `juanigsrz.github.io` (plus the public-IP SSRF guard: loopback / private /
  cloud-metadata addresses are rejected). Max 32 MB, up to 3 redirects.

**Success — `200`**

```json
{
  "ok": true,
  "ms": 1234,            // solver wall time, milliseconds
  "output": "...",       // full solver stdout (trade loops, item summary, checksums, stats)
  "log": "..."           // solver stderr (per-iteration progress)
}
```

The `output` field is the same text the CLI (`ftm < wants.txt`) prints.

**Errors**

| Status | Meaning |
|--------|---------|
| `400`  | Empty body. |
| `413`  | Input larger than 32 MB. |
| `422`  | Solver aborted — usually malformed input. Body: `{ "detail": { "error", "returncode", "log" } }`. |
| `504`  | Solve exceeded 240 s. |

## Limits

- Max input: **32 MB**
- Max solve time: **240 s**

## Examples

### curl

```bash
# full JSON response
curl --data-binary @wants.txt \
  https://juanigsrz--fasttrademaximizer-web.modal.run/solve

# just the solver output (needs jq)
curl -s --data-binary @wants.txt \
  https://juanigsrz--fasttrademaximizer-web.modal.run/solve | jq -r .output

# fetch the wants file from a URL instead of sending a body
curl -s "https://juanigsrz--fasttrademaximizer-web.modal.run/solve?input=https://bgg.activityclub.org/olwlg/377645-officialwants.txt" | jq -r .output
```

> Use `--data-binary`, not `-d`: plain `-d` strips newlines and the parser needs them.

### Python

```python
import requests

with open("wants.txt", "rb") as f:
    r = requests.post(
        "https://juanigsrz--fasttrademaximizer-web.modal.run/solve",
        data=f.read(),
        timeout=300,
    )

r.raise_for_status()
res = r.json()
print(res["output"])
print(f"solved in {res['ms']} ms")
```

### JavaScript (fetch)

```js
const text = await file.text();                  // a File, or any wants string
const r = await fetch(
  "https://juanigsrz--fasttrademaximizer-web.modal.run/solve",
  { method: "POST", headers: { "Content-Type": "text/plain" }, body: text }
);
const res = await r.json();
console.log(res.output);
```

## Input format

Same as the CLI. Options on `#!` lines, optional `!BEGIN-OFFICIAL-NAMES … !END-OFFICIAL-NAMES`
block, then one wishlist line per item:

```
#! ALLOW-DUMMIES REQUIRE-COLONS REQUIRE-USERNAMES
(alice) 1234-GAMEA : 5678-GAMEB 9012-GAMEC
(bob)   5678-GAMEB : 1234-GAMEA
```

See the project README for the full option list.

## Notes

- The endpoint is public and unauthenticated; calls run on the owner's Modal account.
- It scales to zero when idle (first call after a while pays a few seconds of cold start).
