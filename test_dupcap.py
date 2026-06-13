"""dupcap directive: a user receives at most one copy of a protected game,
counting swaps and cash buys together. Runs main.py as a subprocess."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "main.py")

# alice can buy either copy of game G (C1 from bob, C2 from carol).
BUY_BUY = """\
item C1 owner bob ask 10
item C2 owner carol ask 10
bid alice C1 20
bid alice C2 20
"""

# alice swaps her W for C1 and could also buy C2.
SWAP_BUY = """\
item C1 owner bob ask 10
item C2 owner carol ask 10
item W owner alice ask 0
bob : (1for1) C1 -> W
alice : (1for1) W -> C1
bid alice C2 20
"""


def run(text):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        return subprocess.run(
            [sys.executable, MAIN, path],
            capture_output=True, text=True, check=True,
        ).stdout
    finally:
        os.unlink(path)


def alice_g_copies(out):
    """Copies of game G (C1/C2) alice ends up with: swap receipts + cash buys."""
    lines = out.splitlines()
    swap = sum(1 for l in lines
               if l.strip().endswith("-> C1") or l.strip().endswith("-> C2"))
    buys = sum(1 for l in lines if "-> alice" in l and "pays" in l)
    return swap + buys


def test_buy_buy_without_cap_gets_two():
    assert alice_g_copies(run(BUY_BUY)) == 2


def test_buy_buy_with_cap_gets_one():
    assert alice_g_copies(run(BUY_BUY + "dupcap alice C1 C2\n")) == 1


def test_swap_buy_without_cap_gets_two():
    assert alice_g_copies(run(SWAP_BUY)) == 2


def test_swap_buy_with_cap_gets_one():
    assert alice_g_copies(run(SWAP_BUY + "dupcap alice C1 C2\n")) == 1


if __name__ == "__main__":
    test_buy_buy_without_cap_gets_two()
    test_buy_buy_with_cap_gets_one()
    test_swap_buy_without_cap_gets_two()
    test_swap_buy_with_cap_gets_one()
    print("OK: dupcap tests passed")
