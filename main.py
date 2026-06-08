import sys
import os
import re
import gurobipy as gp
from gurobipy import GRB

item_to_id = {}
id_to_item = {}
wishes = []   # (user, give, take, N, M): 'give' games given, 'take' games taken, give at most 'N', take at least 'M'
users = set()
budget = {}   # user -> X_u  (absent => +inf, unconstrained)
owner = {}    # item_id -> user (the original owner)
ask = {}      # item_id -> Z_i (absent => 0)
bids = {}     # (user, item_id) -> Y_ui (max cash the user will pay)


def intern(token):
    if token not in item_to_id:
        item_to_id[token] = len(item_to_id)
        id_to_item[item_to_id[token]] = token
    return item_to_id[token]


def set_owner(iid, u, line):
    if iid in owner and owner[iid] != u:
        raise ValueError(f"Item '{id_to_item[iid]}' has conflicting owners '{owner[iid]}' and '{u}': {line}")
    owner[iid] = u


def parse_wish_body(body, line):
    if not body.startswith('('):
        raise ValueError(f"Missing options: {line}")
    r = body.find(')')
    if r == -1:
        raise ValueError(f"Missing closing ')': {line}")
    if '->' not in body:
        raise ValueError(f"Missing '->': {line}")

    options = body[1:r].strip().split()
    rest = body[r + 1:].strip()

    groups = [part.strip().split() for part in rest.split("->")]
    if len(groups) > 2:
        raise ValueError(f"Non supported amount of groups (max 2): {line}")

    N, M = len(groups[0]), len(groups[1])
    for opt in options:
        match = re.fullmatch(r'(\d+)for(\d+)', opt)
        if not match:
            raise ValueError(f"Option must be in 'NforM' format, e.g., '2for1': {line}")
        N = int(match.group(1))
        M = int(match.group(2))

    give = [intern(t) for t in groups[0]]
    take = [intern(t) for t in groups[1]]
    return give, take, N, M


# Handle input
def parse_file(_file):
    with open(_file, 'r') as f:
        for raw in f:
            line = raw.partition('#')[0].strip()
            if not line:
                continue

            m_user = re.fullmatch(r'user\s+(\S+)\s+budget\s+(\d+)', line)
            m_item = re.fullmatch(r'item\s+(\S+)\s+owner\s+(\S+)(?:\s+ask\s+(\d+))?', line)
            m_bid = re.fullmatch(r'bid\s+(\S+)\s+(\S+)\s+(\d+)', line)

            if m_user:
                users.add(m_user.group(1))
                budget[m_user.group(1)] = int(m_user.group(2))
            elif m_item:
                iid = intern(m_item.group(1))
                u = m_item.group(2)
                users.add(u)
                set_owner(iid, u, raw)
                if m_item.group(3) is not None:
                    ask[iid] = int(m_item.group(3))
            elif m_bid:
                u = m_bid.group(1)
                users.add(u)
                iid = intern(m_bid.group(2))
                bids[(u, iid)] = int(m_bid.group(3))
            elif ':' in line:
                u, _, body = line.partition(':')
                u = u.strip()
                users.add(u)
                give, take, N, M = parse_wish_body(body.strip(), raw)
                for g in give:
                    set_owner(g, u, raw)  # giving an item implies owning it
                wishes.append((u, give, take, N, M))
            else:
                raise ValueError(f"Unrecognized line: {raw}")


parse_file(sys.argv[1])

model = gp.Model()
model.Params.OutputFlag = 0

edge_vars = {}        # (i, j) -> binary var
combo_records = []    # list of (in_pairs, out_pairs); pair = (item_id, var)
spend_swap = {}       # user -> list of (Z_take, take_var): cash legs of swap receipts
in_terms = {}         # item_id -> list of vars where the item is given away in a swap
out_terms = {}        # item_id -> list of vars where the item is received in a swap

combo_node_id = len(item_to_id)


def add_edge(i, j, var):
    edge_vars[(i, j)] = var
    out_terms.setdefault(i, []).append(var)
    in_terms.setdefault(j, []).append(var)


# Build swap / combo variables (unchanged barter structure), recording per-user swap cash legs
for user, send_ids, take_ids, N, M in wishes:
    if len(send_ids) == len(take_ids) == 1:
        e = model.addVar(vtype=GRB.BINARY)
        add_edge(take_ids[0], send_ids[0], e)
        spend_swap.setdefault(user, []).append((ask.get(take_ids[0], 0), e))
        continue

    combo_id = combo_node_id
    combo_node_id += 1

    in_pairs, out_pairs = [], []
    for s in send_ids:
        v = model.addVar(vtype=GRB.BINARY)
        add_edge(combo_id, s, v)
        out_pairs.append((s, v))
    for t in take_ids:
        v = model.addVar(vtype=GRB.BINARY)
        add_edge(t, combo_id, v)
        in_pairs.append((t, v))
        spend_swap.setdefault(user, []).append((ask.get(t, 0), v))

    out_vars = [v for _, v in out_pairs]
    in_vars = [v for _, v in in_pairs]

    # These ensure that no individual edge is active unless the whole combo is active
    active = model.addVar(vtype=GRB.BINARY)
    model.addConstr(gp.quicksum(out_vars) <= len(out_vars) * active)
    model.addConstr(gp.quicksum(in_vars) <= len(in_vars) * active)

    # Total outgoing (sent) <= N if combo is active, make sure at least 1 item is sent
    model.addConstr(gp.quicksum(out_vars) <= N * active)
    model.addConstr(active <= gp.quicksum(out_vars))

    # Total incoming (received) >= M if combo is active
    model.addConstr(M * active <= gp.quicksum(in_vars))

    combo_records.append((in_pairs, out_pairs))

# Cash purchase variables: created only for bids that clear the ask and aren't self-buys
buy = {}
for (u, iid), y in bids.items():
    o = owner.get(iid)
    if o is None:
        raise ValueError(f"Cannot bid on item '{id_to_item[iid]}' with no declared owner")
    if u == o:
        continue  # don't buy your own item
    if y < ask.get(iid, 0):
        continue  # bid doesn't clear the ask -> edge filtered out
    buy[(u, iid)] = model.addVar(vtype=GRB.BINARY)

# A wish's take-item may also be acquired with cash (implicit bid, willing to pay the ask),
# funded by the net budget. Lets a swap intent complete via a cash chain when no barter swap
# closes -- e.g. B sells B_GAME for cash and uses the proceeds to buy its wished C_GAME.
# Gated on money being present so pure-barter instances keep strict swap reciprocity.
money_present = bool(ask) or bool(budget) or bool(bids)
if money_present:
    for user, send_ids, take_ids, N, M in wishes:
        for t in take_ids:
            if user == owner.get(t) or (user, t) in buy:
                continue
            buy[(user, t)] = model.addVar(vtype=GRB.BINARY)

buy_terms = {}  # item_id -> list of buy vars
for (u, iid), v in buy.items():
    buy_terms.setdefault(iid, []).append(v)

real_item_ids = set(item_to_id.values())

# Build model constraints (swap balance kept; cash competes for the same single slot)
for node in real_item_ids:
    ins = in_terms.get(node, [])
    outs = out_terms.get(node, [])
    if ins and outs:
        model.addConstr(gp.quicksum(ins) == gp.quicksum(outs))
    elif ins:
        model.addConstr(gp.quicksum(ins) == 0)   # given but no swap wants it -> can only leave via cash
    elif outs:
        model.addConstr(gp.quicksum(outs) == 0)  # wanted but never offered for swap
    if ins:
        model.addConstr(gp.quicksum(ins) <= 1)
    if node in buy_terms:
        model.addConstr(gp.quicksum(outs) + gp.quicksum(buy_terms[node]) <= 1)

# Bucket buys and items by user once, so the budget build is linear instead of O(users^2).
buys_by_user = {}    # user -> list of (item_id, var)
for (u, iid), v in buy.items():
    buys_by_user.setdefault(u, []).append((iid, v))
items_by_owner = {}  # user -> list of item_id
for iid, o in owner.items():
    items_by_owner.setdefault(o, []).append(iid)

# Per-user net budget: spend (swap receipts + cash buys) minus earnings (own items leaving) <= X_u
spend_data = {}  # user -> list of (coeff, var) for reporting
earn_data = {}   # user -> list of (coeff, var) for reporting
for u in users:
    spend = [(c, v) for (c, v) in spend_swap.get(u, []) if c]
    spend += [(ask.get(iid, 0), v) for (iid, v) in buys_by_user.get(u, []) if ask.get(iid, 0)]
    earn = []
    for iid in items_by_owner.get(u, []):
        z = ask.get(iid, 0)
        if z:
            earn += [(z, v) for v in in_terms.get(iid, [])]
            earn += [(z, v) for v in buy_terms.get(iid, [])]
    spend_data[u] = spend
    earn_data[u] = earn
    if u in budget and (spend or earn):
        lhs = gp.quicksum(c * v for c, v in spend) - gp.quicksum(c * v for c, v in earn)
        model.addConstr(lhs <= budget[u])

# Maximize total trades (swap item-moves + cash purchases); tie-break toward barter swaps so a
# pure swap is reported as a swap rather than an equivalent pair of cash purchases.
swaps = list(edge_vars.values())
buys = list(buy.values())
eps = 1.0 / (len(swaps) + 1) if swaps else 0.0

_time_limit = os.environ.get("FTM_TIME_LIMIT")
if _time_limit:
    model.Params.TimeLimit = float(_time_limit)

model.setObjective(gp.quicksum(buys) + gp.quicksum((1.0 + eps) * s for s in swaps), GRB.MAXIMIZE)
model.optimize()

_STATUS = {GRB.OPTIMAL: "Optimal", GRB.TIME_LIMIT: "TimeLimit", GRB.INFEASIBLE: "Infeasible"}
status = _STATUS.get(model.Status, f"Status{model.Status}")
if status != "Optimal":
    print(f"WARNING: solver status is {status}", file=sys.stderr)

if os.environ.get("FTM_STATS"):
    obj = model.ObjVal if model.SolCount > 0 else float("nan")
    print(
        f"STATS swap_vars={len(swaps)} buy_vars={len(buys)} combos={len(combo_records)} "
        f"items={len(real_item_ids)} status={status} obj={obj:.0f} "
        f"runtime={model.Runtime:.3f}",
        file=sys.stderr,
    )

if model.SolCount == 0:
    print("No solution found.", file=sys.stderr)
    sys.exit(0)


def active(var):
    return var.X > 0.5


print("\nTrade Results:")
for (i, j), var in edge_vars.items():
    if active(var) and i in id_to_item and j in id_to_item:
        print(f"{id_to_item[j]} -> {id_to_item[i]}")

for in_pairs, out_pairs in combo_records:
    if any(active(v) for _, v in in_pairs + out_pairs):
        sent = [id_to_item[s] for s, v in out_pairs if active(v)]
        taken = [id_to_item[t] for t, v in in_pairs if active(v)]
        print(*sent, sep=' ', end='')
        print(" -> ", end='')
        print(*taken, sep=' ')

show_money = bool(buy) or bool(ask) or bool(budget)
if show_money:
    cash_moves = [(u, iid) for (u, iid), v in buy.items() if active(v)]
    if cash_moves:
        print("\nCash Purchases:")
        for (u, iid) in cash_moves:
            o = owner[iid]
            print(f"{id_to_item[iid]}: {o} -> {u}  ({u} pays {o} ${ask.get(iid, 0)})")

    print("\nCash Summary:")
    for u in sorted(users):
        spent = sum(c * v.X for c, v in spend_data[u])
        earned = sum(c * v.X for c, v in earn_data[u])
        cap = budget[u] if u in budget else "inf"
        print(f"  {u}: spent ${spent:g}, earned ${earned:g}, net ${spent - earned:g} (cap ${cap})")
