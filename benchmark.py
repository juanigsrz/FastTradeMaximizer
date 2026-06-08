"""Sweep instance sizes and report solver scaling.

Generates instances with generate_testcase.generate, runs main.py with a HiGHS time limit,
and tabulates variable counts, solver runtime (HiGHS), and wall time (incl. Python build).
Stops a sweep once a solve fails to prove optimality within the time limit.

Usage:
    python benchmark.py                       # default money sweep
    python benchmark.py --money 0             # pure-barter sweep
    python benchmark.py --users 10 50 200 --time-limit 60
"""
import argparse
import os
import subprocess
import tempfile
import time

import generate_testcase as G

HERE = os.path.dirname(os.path.abspath(__file__))
PY = os.path.join(HERE, "venv/bin/python")
MAIN = os.path.join(HERE, "main.py")


def run_one(cfg, time_limit):
    users, items, wants, money, bundle, seed = cfg
    text = G.generate(users, items, wants, money, bundle, seed)
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(text)
        path = f.name
    env = dict(os.environ, FTM_STATS="1", FTM_TIME_LIMIT=str(time_limit))
    t0 = time.perf_counter()
    try:
        r = subprocess.run([PY, MAIN, path], env=env, capture_output=True,
                           text=True, timeout=time_limit + 120)
        killed = False
    except subprocess.TimeoutExpired:
        r = None
        killed = True
    finally:
        os.unlink(path)
    wall = time.perf_counter() - t0
    stats = ""
    if r is not None:
        for line in r.stderr.splitlines():
            if line.startswith("STATS"):
                stats = line
    return {"wall": wall, "stats": stats, "killed": killed}


def parse_stats(s):
    return dict(tok.split("=", 1) for tok in s.split()[1:] if "=" in tok)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--time-limit", type=float, default=20)
    ap.add_argument("--items", type=int, default=8)
    ap.add_argument("--wants", type=int, default=8)
    ap.add_argument("--money", type=float, default=0.6)
    ap.add_argument("--bundle", type=float, default=0.1)
    ap.add_argument("--users", type=int, nargs="+",
                    default=[10, 25, 50, 100, 200, 400, 800, 1600])
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()

    print(f"# money={a.money} bundle={a.bundle} items={a.items} wants={a.wants} "
          f"time_limit={a.time_limit}s")
    hdr = (f"{'users':>6} {'swap':>7} {'buy':>7} {'combos':>7} {'items':>7} "
           f"{'status':>14} {'obj':>8} {'solve_s':>9} {'wall_s':>8}")
    print(hdr)
    print("-" * len(hdr))
    for u in a.users:
        res = run_one((u, a.items, a.wants, a.money, a.bundle, a.seed), a.time_limit)
        if res["killed"]:
            print(f"{u:>6} {'KILLED (wall exceeded time_limit+120)':>60}")
            break
        d = parse_stats(res["stats"]) if res["stats"] else {}
        print(f"{u:>6} {d.get('swap_vars','?'):>7} {d.get('buy_vars','?'):>7} "
              f"{d.get('combos','?'):>7} {d.get('items','?'):>7} "
              f"{d.get('status','?'):>14} {d.get('obj','?'):>8} "
              f"{d.get('runtime','?'):>9} {res['wall']:>8.2f}")
        if d.get("status", "") not in ("Optimal", ""):
            print(f"  -> stopped: solver hit time limit (status={d.get('status')})")
            break


if __name__ == "__main__":
    main()
