import sys
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

edge_vars = {}
combo_vars = []
combovar_to_item = {}
spend_swap = {}  # user -> LinExpr of Z_take * (take var), cash legs of swap receipts

combo_node_id = len(item_to_id)


def add_spend_swap(u, iid, var):
    spend_swap[u] = spend_swap.get(u, gp.LinExpr()) + ask.get(iid, 0) * var


# Build swap / combo variables (unchanged barter structure), recording per-user swap cash legs
for user, send_ids, take_ids, N, M in wishes:
    if len(send_ids) == len(take_ids) == 1:
        e = model.addVar(vtype=GRB.BINARY)
        edge_vars[(take_ids[0], send_ids[0])] = e
        add_spend_swap(user, take_ids[0], e)
        continue

    combo_id = combo_node_id
    combo_node_id += 1

    in_vars, out_vars = [], []

    for s in send_ids:
        name = str(s)
        v = model.addVar(vtype=GRB.BINARY, name=name)
        edge_vars[(combo_id, s)] = v
        combovar_to_item[name] = s
        out_vars.append(v)

    for t in take_ids:
        name = str(t)
        v = model.addVar(vtype=GRB.BINARY, name=name)
        edge_vars[(t, combo_id)] = v
        combovar_to_item[name] = t
        in_vars.append(v)
        add_spend_swap(user, t, v)

    # These ensure that no individual edge is active unless the whole combo is active
    combo_active = model.addVar(vtype=GRB.BINARY)
    model.addConstr(gp.quicksum(out_vars) <= len(out_vars) * combo_active)
    model.addConstr(gp.quicksum(in_vars) <= len(in_vars) * combo_active)

    # Total outgoing (sent) <= N if combo is active, make sure at least 1 item is sent
    model.addConstr(gp.quicksum(out_vars) <= N * combo_active)
    model.addConstr(1 * combo_active <= gp.quicksum(out_vars))

    # Total incoming (received) >= M if combo is active
    model.addConstr(M * combo_active <= gp.quicksum(in_vars))

    combo_vars.append((in_vars, out_vars))

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
    buy[(u, iid)] = model.addVar(vtype=GRB.BINARY, name=f"buy_{u}_{iid}")

in_sum = {}
out_sum = {}
for (i, j) in edge_vars:
    out_sum[i] = out_sum.get(i, gp.LinExpr()) + edge_vars[(i, j)]
    in_sum[j] = in_sum.get(j, gp.LinExpr()) + edge_vars[(i, j)]

buy_sum = {}  # item_id -> LinExpr of sum_v buy[(v,i)]
for (u, iid), v in buy.items():
    buy_sum[iid] = buy_sum.get(iid, gp.LinExpr()) + v

real_item_ids = set(item_to_id.values())

# Build model constraints (swap balance kept; cash competes for the same single slot)
for node in real_item_ids:
    in_expr = in_sum.get(node, gp.LinExpr())
    out_expr = out_sum.get(node, gp.LinExpr())
    model.addConstr(in_expr == out_expr)
    model.addConstr(in_expr <= 1)
    if node in buy_sum:
        model.addConstr(out_expr + buy_sum[node] <= 1)

# Per-user net budget: spend (swap receipts + cash buys) minus earnings (own items leaving) <= X_u
spend_expr = {}
earn_expr = {}
for u in users:
    spend = spend_swap.get(u, gp.LinExpr())
    for (uu, iid), v in buy.items():
        if uu == u:
            spend = spend + ask.get(iid, 0) * v
    earn = gp.LinExpr()
    for iid, o in owner.items():
        if o == u:
            depart = in_sum.get(iid, gp.LinExpr()) + buy_sum.get(iid, gp.LinExpr())
            earn = earn + ask.get(iid, 0) * depart
    spend_expr[u] = spend
    earn_expr[u] = earn
    if u in budget:
        model.addConstr(spend - earn <= budget[u], name=f"netbudget_{u}")

# Solve to maximize number of trades (swap item-moves + cash purchases)
model.setObjective(gp.quicksum(edge_vars.values()) + gp.quicksum(buy.values()), GRB.MAXIMIZE)
model.optimize()


print("\nTrade Results:")
for (i, j) in edge_vars:
    if edge_vars[(i, j)].X > 0.5 and i in id_to_item and j in id_to_item:
        print(f"{id_to_item[j]} -> {id_to_item[i]}")

for in_vars, out_vars in combo_vars:
    if any(v.X > 0.5 for v in in_vars + out_vars):
        sent = [id_to_item[combovar_to_item[v.VarName]] for v in out_vars if v.X > 0.5]
        taken = [id_to_item[combovar_to_item[v.VarName]] for v in in_vars if v.X > 0.5]
        print(*sent, sep=' ', end='')
        print(" -> ", end='')
        print(*taken, sep=' ')

show_money = bool(buy) or bool(ask) or bool(budget)
if show_money:
    cash_moves = [(u, iid) for (u, iid), v in buy.items() if v.X > 0.5]
    if cash_moves:
        print("\nCash Purchases:")
        for (u, iid) in cash_moves:
            o = owner[iid]
            print(f"{id_to_item[iid]}: {o} -> {u}  ({u} pays {o} ${ask.get(iid, 0)})")

    print("\nCash Summary:")
    for u in sorted(users):
        spent = spend_expr[u].getValue()
        earned = earn_expr[u].getValue()
        cap = budget[u] if u in budget else "inf"
        print(f"  {u}: spent ${spent:g}, earned ${earned:g}, net ${spent - earned:g} (cap ${cap})")
