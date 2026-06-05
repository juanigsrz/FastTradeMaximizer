# FastTradeMaximizer on Modal + GitHub Pages

Run the solver as a hosted service: a [Modal](https://modal.com) container compiles
the C++ solver and exposes an HTTP endpoint; a static page on GitHub Pages calls it.

```
 browser (GitHub Pages, docs/index.html)
        |  POST wants text
        v
 Modal web endpoint  --pipe stdin/stdout-->  ftm  (compiled into the image)
```

## 1. Deploy the Modal service

```bash
pip install modal
modal token new                 # one-time auth
modal deploy modal_app.py       # run from the repo root (it copies main.cpp + *.hpp)
```

Modal prints a URL like:

```
https://<workspace>--fasttrademaximizer-web.modal.run
```

The solver is built once at image-build time (`g++ -std=c++17 -O3`), so requests pay
no compile cost. Endpoints:

- `GET  /`       health check
- `POST /solve`  body = raw wants file text → `{ ok, ms, output, log }`

Quick test:

```bash
curl -s --data-binary @testcases/BR2024May.txt \
     https://<workspace>--fasttrademaximizer-web.modal.run/solve | python3 -m json.tool
```

Local dev with autoreload: `modal serve modal_app.py`.

## 2. Point the frontend at it

Open the deployed page and paste the URL into the **Modal endpoint** field (it is
saved in your browser). To hardcode it instead, set `MODAL_URL` near the top of the
`<script>` in `docs/index.html`.

## 3. Publish the frontend on GitHub Pages

Settings → Pages → Build and deployment → **Deploy from a branch**, then pick:

- Branch: `modal` (or wherever `docs/` lives)
- Folder: `/docs`

Page goes live at `https://<user>.github.io/FastTradeMaximizer/`. (Or copy
`docs/index.html` into a `<user>.github.io` repo to serve it at the domain root.)

## Notes / limits

- **CORS** is wide open (`allow_origins=["*"]`) so any page can call it. Restrict it
  to your Pages domain in `modal_app.py` if you want to lock it down.
- **Size / time caps**: 32 MB input, 240 s solve (see constants in `modal_app.py`).
- **Cost**: the endpoint scales to zero when idle; you only pay for solve time. CPU/memory
  are set on the `@app.function` decorator.
- Malformed input makes the solver abort; the service returns HTTP 422 with the tail of
  the solver log to help debug.
