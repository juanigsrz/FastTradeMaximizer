import sys
import re
import gurobipy as gp
from gurobipy import GRB

item_to_id = {}
id_to_item = {}
wishes = []

# Handle input
with open(sys.argv[1], 'r') as f:
    for line in f:
        formattedLine = line.partition('#')[0].strip()
        groups = [part.strip().split() for part in formattedLine.split("->")]
        if(len(groups) > 2):
            raise ValueError(f"Non supported amount of groups (max 2): {line}", line)
        
        wish = ([],[])

        for i in range(0, len(groups)):
            for token in groups[i]:
                if token not in item_to_id:
                    item_to_id[token] = len(item_to_id)
                    id_to_item[item_to_id[token]] = token
                wish[i].append(item_to_id[token])

        wishes.append(wish)

model = gp.Model()

edge_vars = {}
combo_vars = []
combo_labels = []

combo_node_id = len(item_to_id)

# Build model variables
for give_ids, get_ids in wishes:
    if len(give_ids) == len(get_ids) == 1:
        edge_vars[(give_ids[0], get_ids[0])] = model.addVar(vtype=GRB.BINARY)
        continue
    
    combo_id = combo_node_id
    combo_node_id += 1

    in_vars, out_vars = [], []

    for i in get_ids:
        v = model.addVar(vtype=GRB.BINARY)
        edge_vars[(combo_id, i)] = v
        in_vars.append(v)

    for o in give_ids:
        v = model.addVar(vtype=GRB.BINARY)
        edge_vars[(o, combo_id)] = v
        out_vars.append(v)

    model.addConstr(len(give_ids) * gp.quicksum(in_vars) == len(get_ids) * gp.quicksum(out_vars))
    combo_vars.append((in_vars, out_vars))
    combo_labels.append(f"{' + '.join(id_to_item[o] for o in give_ids)} -> {' + '.join(id_to_item[i] for i in get_ids)}")

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
        print(f"{id_to_item[i]} -> {id_to_item[j]}")

for i, (in_vars, out_vars) in enumerate(combo_vars):
    if any(v.X > 0.5 for v in in_vars + out_vars):
        print(combo_labels[i])
