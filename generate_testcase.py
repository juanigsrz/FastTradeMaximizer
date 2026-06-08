"""Generate a random math-trade instance in the money-clearinghouse format.

Each user owns some items and wishes for items owned by others, producing cross-user
trade cycles. Money is opt-in via --money (density 0..1): a fraction of items get asks,
a fraction of users get finite (tight) budgets, and cross-user cash bids are added.
Tight budgets + many bids stress the NP-hard budget-knapsack part of the MIP.

Usage:
    python generate_testcase.py --users 100 --items 8 --wants 8 --money 0.5 --bundle 0.4 --seed 1 --out inst.txt
"""
import argparse
import random
import sys


def generate(users, items_per_user, wants, money, bundle, seed):
    rng = random.Random(seed)

    owner = {}                       # item -> user index
    owned = {u: [] for u in range(users)}
    gid = 0
    for u in range(users):
        for _ in range(items_per_user):
            name = f"g{gid}"
            owner[name] = u
            owned[u].append(name)
            gid += 1
    all_items = list(owner)

    ask = {}
    if money:
        for it in all_items:
            if rng.random() < money:
                ask[it] = rng.randint(5, 50)

    def pick_other(u):
        while True:
            it = rng.choice(all_items)
            if owner[it] != u:
                return it

    def pick_others(u, k):
        chosen = set()
        while len(chosen) < k:
            chosen.add(pick_other(u))
        return list(chosen)

    lines = []

    # Finite (deliberately tight) budgets for a money fraction of users; others stay infinite.
    if money:
        for u in range(users):
            if rng.random() < money:
                lines.append(f"user u{u} budget {rng.randint(0, 60)}")

    # Explicit item declarations so every owner/ask is known (enables sales and bids).
    for it in all_items:
        u = owner[it]
        if it in ask:
            lines.append(f"item {it} owner u{u} ask {ask[it]}")
        else:
            lines.append(f"item {it} owner u{u}")

    # Wishes: each user wants items owned by others; some are N-to-M bundles.
    if users >= 2:
        for u in range(users):
            if not owned[u]:
                continue
            for _ in range(wants):
                r = rng.random()
                if r < bundle and len(owned[u]) >= 2:
                    gives = rng.sample(owned[u], 2)
                    takes = pick_others(u, 1)
                    opt = "2for1"
                elif r < 2 * bundle:
                    gives = [rng.choice(owned[u])]
                    takes = pick_others(u, 2)
                    opt = "1for2"
                else:
                    gives = [rng.choice(owned[u])]
                    takes = pick_others(u, 1)
                    opt = "1for1"
                lines.append(f"u{u}: ({opt}) {' '.join(gives)} -> {' '.join(takes)}")

    # Explicit cross-user cash bids (above the ask so they clear).
    if money and users >= 2:
        for u in range(users):
            for _ in range(wants):
                if rng.random() < money:
                    it = pick_other(u)
                    y = ask.get(it, 0) + rng.randint(0, 20)
                    lines.append(f"bid u{u} {it} {y}")

    return "\n".join(lines) + "\n"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--users", type=int, default=50)
    p.add_argument("--items", type=int, default=8, help="items owned per user")
    p.add_argument("--wants", type=int, default=8, help="wishes per user")
    p.add_argument("--money", type=float, default=0.0, help="money density 0..1")
    p.add_argument("--bundle", type=float, default=0.0, help="prob a wish is N-to-M")
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--out", default="-", help="output file ('-' for stdout)")
    a = p.parse_args()

    text = generate(a.users, a.items, a.wants, a.money, a.bundle, a.seed)
    if a.out == "-":
        sys.stdout.write(text)
    else:
        with open(a.out, "w") as f:
            f.write(text)


if __name__ == "__main__":
    main()
