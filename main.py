import sys
import re
import gurobipy as gp
from gurobipy import GRB

item_to_id = {}
id_to_item = {}
wishes = [] # (give,take,N,M): 'give' list of games to be given, 'take' list of games to be taken, give at most 'N', take at least 'M'

# Handle input
def parse_file(_file):
    with open(_file, 'r') as f:
        for line in f:
            formattedLine = line.partition('#')[0].strip()
            if not formattedLine.startswith('('):
                raise ValueError(f"Missing options: {line}")
            
            r = formattedLine.find(')')
            if r == -1:
                raise ValueError(f"Missing closing ')': {line}")
            
            if '->' not in formattedLine:
                raise ValueError(f"Missing '->': {line}")

            options = formattedLine[1:r].strip().split()
            formattedLine = formattedLine[r+1:].strip()

            groups = [part.strip().split() for part in formattedLine.split("->")]
            if(len(groups) > 2):
                raise ValueError(f"Non supported amount of groups (max 2): {line}")

            N, M = len(groups[0]), len(groups[1])
            for opt in options:
                match = re.fullmatch(r'(\d+)for(\d+)', opt)
                if not match:
                    raise ValueError(f"Option must be in 'NforM' format, e.g., '2for1': {line}")
                N = int(match.group(1))
                M = int(match.group(2))
            
            wish = ([], [], N, M)
            for i in range(0, len(groups)):
                for token in groups[i]:
                    if token not in item_to_id:
                        item_to_id[token] = len(item_to_id)
                        id_to_item[item_to_id[token]] = token
                    wish[i].append(item_to_id[token])

            wishes.append(wish)


parse_file(sys.argv[1])

model = gp.Model()

edge_vars = {}
combo_vars = []
combo_labels = []
combovar_to_item = {}

combo_node_id = len(item_to_id)

# Build model variables
for send_ids, take_ids, N, M in wishes:
    if len(send_ids) == len(take_ids) == 1:
        edge_vars[(take_ids[0], send_ids[0])] = model.addVar(vtype=GRB.BINARY)
        continue
    
    combo_id = combo_node_id
    combo_node_id += 1

    in_vars, out_vars = [], []

    for s in send_ids:
        var_name = str(s)
        v = model.addVar(vtype=GRB.BINARY, name=var_name)
        edge_vars[(combo_id, s)] = v
        combovar_to_item[var_name] = s
        out_vars.append(v)
    
    for t in take_ids:
        var_name = str(t)
        v = model.addVar(vtype=GRB.BINARY, name=var_name)
        edge_vars[(t, combo_id)] = v
        combovar_to_item[var_name] = t
        in_vars.append(v)

    # These ensure that no individual edge is active unless the whole combo is active
    combo_active = model.addVar(vtype=GRB.BINARY)
    model.addConstr(gp.quicksum(out_vars) <= len(out_vars) * combo_active)
    model.addConstr(gp.quicksum(in_vars) <= len(in_vars) * combo_active)

    # Total outgoing (sent) ≤ N if combo is active, make sure at least 1 item is sent
    model.addConstr(gp.quicksum(out_vars) <= N * combo_active)
    model.addConstr(1 * combo_active <= gp.quicksum(out_vars))

    # Total incoming (received) ≥ M if combo is active
    model.addConstr(M * combo_active <= gp.quicksum(in_vars))

    combo_vars.append((in_vars, out_vars))
    combo_labels.append(f"{' '.join(id_to_item[s] for s in send_ids)} -> {' '.join(id_to_item[t] for t in take_ids)}")

in_sum = {}
out_sum = {}

# Build model constraints
for (i,j) in edge_vars:
    out_sum[i] = out_sum.get(i, gp.LinExpr()) + edge_vars[(i,j)]
    in_sum[j] = in_sum.get(j, gp.LinExpr()) + edge_vars[(i,j)]

real_item_ids = set(item_to_id.values())

for node in real_item_ids:
    in_expr = in_sum.get(node, gp.LinExpr())
    out_expr = out_sum.get(node, gp.LinExpr())
    model.addConstr(in_expr == out_expr)
    model.addConstr(in_expr <= 1)

# Solve to maximize number of edges used
model.setObjective(gp.quicksum(edge_vars.values()), GRB.MAXIMIZE)
model.optimize()


print("\nTrade Results:")
for (i,j) in edge_vars:
    if edge_vars[(i,j)].X > 0.5 and i in id_to_item and j in id_to_item:
        print(f"{id_to_item[j]} -> {id_to_item[i]}")

for i, (in_vars, out_vars) in enumerate(combo_vars):
    if any(v.X > 0.5 for v in in_vars + out_vars):
        sent =  [ id_to_item[combovar_to_item[v.VarName]] for v in out_vars if v.X > 0.5 ]
        taken = [ id_to_item[combovar_to_item[v.VarName]] for v in in_vars if v.X > 0.5 ]

        print(*sent, sep=' ', end='')
        print(" -> ", end='')
        print(*taken, sep=' ')
