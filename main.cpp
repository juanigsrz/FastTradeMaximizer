#pragma GCC optimize("O3")
#pragma GCC optimize("unroll-loops")
// #pragma GCC target("avx,avx2,sse,sse2,sse3,sse4,popcnt")

#include <bits/stdc++.h>
#define long long long // XD

using namespace std;

struct linked_lists {
    int L, N;
    vector<int> next, prev;

    // L: lists are [0...L), N: integers are [0...N)
    explicit linked_lists(int L = 0, int N = 0) { assign(L, N); }

    int rep(int l) const { return l + N; }
    int head(int l) const { return next[rep(l)]; }
    int tail(int l) const { return prev[rep(l)]; }
    bool empty(int l) const { return next[rep(l)] == rep(l); }

    void init(int l, int n) { meet(rep(l), n, rep(l)); }
    void clear(int l) { next[rep(l)] = prev[rep(l)] = rep(l); }

    void push_front(int l, int n) { assert(l < L && n < N), meet(rep(l), n, head(l)); }
    void push_back(int l, int n) { assert(l < L && n < N), meet(tail(l), n, rep(l)); }
    void insert_before(int i, int n) { assert(i < N && n < N), meet(prev[i], n, i); }
    void insert_after(int i, int n) { assert(i < N && n < N), meet(i, n, next[i]); }
    void erase(int n) { assert(n < N), meet(prev[n], next[n]); }
    void pop_front(int l) { assert(l < L && !empty(l)), meet(rep(l), next[head(l)]); }
    void pop_back(int l) { assert(l < L && !empty(l)), meet(prev[tail(l)], rep(l)); }

    void splice_front(int l, int b) {
        if (l != b && !empty(b))
            meet(tail(b), head(l)), meet(rep(l), head(b)), clear(b);
    }
    void splice_back(int l, int b) {
        if (l != b && !empty(b))
            meet(tail(l), head(b)), meet(tail(b), rep(l)), clear(b);
    }

    void clear() {
        iota(begin(next) + N, end(next), N);
        iota(begin(prev) + N, end(prev), N);
    }
    void assign(int L, int N) {
        this->L = L, this->N = N;
        next.resize(N + L), prev.resize(N + L), clear();
    }

  private:
    inline void meet(int u, int v) { next[u] = v, prev[v] = u; }
    inline void meet(int u, int v, int w) { meet(u, v), meet(v, w); }
};

#define FOR_EACH_IN_LINKED_LIST(i, l, lists) \
    for (int z##i = l, i = lists.head(z##i); i != lists.rep(z##i); i = lists.next[i])

#define FOR_EACH_IN_LINKED_LIST_REVERSE(i, l, lists) \
    for (int z##i = l, i = lists.tail(z##i); i != lists.rep(z##i); i = lists.prev[i])

template <typename Flow = long, typename Cost = long, typename CostSum = Cost>
struct network_simplex {
    explicit network_simplex(int V) : V(V), node(V + 1) {}

    void add(int u, int v, Flow lower, Flow upper, Cost cost) {
        assert(0 <= u && u < V && 0 <= v && v < V);
        edge.push_back({{u, v}, lower, upper, cost}), E++;
    }

    void add_supply(int u, Flow supply) { node[u].supply += supply; }
    void add_demand(int u, Flow demand) { node[u].supply -= demand; }

    auto get_supply(int u) const { return node[u].supply; }
    auto get_potential(int u) const { return node[u].pi; }
    auto get_flow(int e) const { return edge[e].flow; }

    auto get_circulation_cost() const {
        CostSum sum = 0;
        for (int e = 0; e < E; e++) {
            sum += edge[e].flow * CostSum(edge[e].cost);
        }
        return sum;
    }

    auto mincost_circulation() {
        static constexpr bool INFEASIBLE = false, OPTIMAL = true;

        Flow sum_supply = 0;
        for (int u = 0; u < V; u++) {
            sum_supply += node[u].supply;
        }
        if (sum_supply != 0) {
            return INFEASIBLE;
        }
        for (int e = 0; e < E; e++) {
            if (edge[e].lower > edge[e].upper) {
                return INFEASIBLE;
            }
        }

        init();
        int in_arc = select_pivot_edge();
        while (in_arc != -1) {
            pivot(in_arc);
            in_arc = select_pivot_edge();
        }

        for (int e = 0; e < E; e++) {
            auto [u, v] = edge[e].node;
            edge[e].flow += edge[e].lower;
            edge[e].upper += edge[e].lower;
            node[u].supply += edge[e].lower;
            node[v].supply -= edge[e].lower;
        }
        for (int e = E; e < E + V; e++) {
            if (edge[e].flow != 0) {
                edge.resize(E);
                return INFEASIBLE;
            }
        }
        edge.resize(E);
        return OPTIMAL;
    }

  private:
    enum ArcState : int8_t { STATE_UPPER = -1, STATE_TREE = 0, STATE_LOWER = 1 };

    struct Node {
        int parent, pred;
        Flow supply;
        CostSum pi;
    };
    struct Edge {
        int node[2];
        Flow lower, upper;
        Cost cost;
        Flow flow = 0;
        ArcState state = STATE_LOWER;
    };
    int V, E = 0, next_arc = 0, block_size = 0;
    vector<Node> node;
    vector<Edge> edge;
    linked_lists children;
    vector<int> bfs; // scratchpad for pi bfs and upwards walk

    auto reduced_cost(int e) const {
        auto [u, v] = edge[e].node;
        return edge[e].cost + node[u].pi - node[v].pi;
    }
    void init() {
        Cost slack_cost = 0;

        for (int e = 0; e < E; e++) {
            auto [u, v] = edge[e].node;
            edge[e].flow = 0;
            edge[e].state = STATE_LOWER;
            edge[e].upper -= edge[e].lower;
            node[u].supply -= edge[e].lower;
            node[v].supply += edge[e].lower;
            slack_cost += edge[e].cost < 0 ? -edge[e].cost : edge[e].cost;
        }

        edge.resize(E + V);
        bfs.resize(V + 1);
        children.assign(V + 1, V + 1);
        next_arc = 0;

        // Remove non-zero lower bounds
        int root = V;
        node[root] = {-1, -1, 0, 0};

        for (int u = 0, e = E; u < V; u++, e++) {
            node[u].parent = root, node[u].pred = e;
            children.push_back(root, u);
            auto supply = node[u].supply;
            if (supply >= 0) {
                node[u].pi = -slack_cost;
                edge[e] = {{u, root}, 0, supply, slack_cost, supply, STATE_TREE};
            } else {
                node[u].pi = slack_cost;
                edge[e] = {{root, u}, 0, -supply, slack_cost, -supply, STATE_TREE};
            }
        }

        block_size = max(int(ceil(sqrt(V + 1))), min(10, V + 1));
    }

    int select_pivot_edge() { // lemon-like block search
        int in_arc = -1;
        int count = block_size, seen_edges = E + V;
        Cost minimum = 0;
        for (int e = next_arc; seen_edges-- > 0; e = e + 1 == E + V ? 0 : e + 1) {
            if (minimum > edge[e].state * reduced_cost(e)) {
                minimum = edge[e].state * reduced_cost(e);
                in_arc = e;
            }
            if (--count == 0 && minimum < 0) {
                next_arc = e + 1 == E + V ? 0 : e + 1;
                return in_arc;
            } else if (count == 0) {
                count = block_size;
            }
        }
        return in_arc;
    }

    void pivot(int in_arc) {
        // Find join node
        auto [u_in, v_in] = edge[in_arc].node;
        int a = u_in, b = v_in;
        while (a != b) {
            a = node[a].parent == -1 ? v_in : node[a].parent;
            b = node[b].parent == -1 ? u_in : node[b].parent;
        }
        int join = a;

        // we add flow to source->target
        int source = edge[in_arc].state == STATE_LOWER ? u_in : v_in;
        int target = edge[in_arc].state == STATE_LOWER ? v_in : u_in;

        enum OutArcSide { SAME_EDGE, FIRST_SIDE, SECOND_SIDE };
        Flow flow_delta = edge[in_arc].upper;
        OutArcSide side = SAME_EDGE;
        int u_out = -1;

        // Go up the cycle from source to the join node
        for (int u = source; u != join; u = node[u].parent) {
            int e = node[u].pred;
            bool edge_down = u == edge[e].node[1];
            Flow d = edge_down ? edge[e].upper - edge[e].flow : edge[e].flow;
            if (flow_delta > d) {
                flow_delta = d;
                u_out = u;
                side = FIRST_SIDE;
            }
        }

        // Go up the cycle from target to the join node
        for (int u = target; u != join; u = node[u].parent) {
            int e = node[u].pred;
            bool edge_up = u == edge[e].node[0];
            Flow d = edge_up ? edge[e].upper - edge[e].flow : edge[e].flow;
            if (flow_delta >= d) {
                flow_delta = d;
                u_out = u;
                side = SECOND_SIDE;
            }
        }

        // Put u_in on the same side as u_out
        u_in = side == FIRST_SIDE ? source : target;
        v_in = side == FIRST_SIDE ? target : source;

        // Augment along the cycle
        if (flow_delta) {
            auto delta = edge[in_arc].state * flow_delta;
            edge[in_arc].flow += delta;
            for (int u = edge[in_arc].node[0]; u != join; u = node[u].parent) {
                int e = node[u].pred;
                edge[e].flow += (u == edge[e].node[0] ? -1 : +1) * delta;
            }
            for (int u = edge[in_arc].node[1]; u != join; u = node[u].parent) {
                int e = node[u].pred;
                edge[e].flow += (u == edge[e].node[0] ? +1 : -1) * delta;
            }
        }

        if (side == SAME_EDGE) {
            edge[in_arc].state = ArcState(-edge[in_arc].state);
            return;
        }

        int out_arc = node[u_out].pred;
        edge[in_arc].state = STATE_TREE;
        edge[out_arc].state = edge[out_arc].flow ? STATE_UPPER : STATE_LOWER;

        int i = 0, S = 0;
        for (int u = u_in; u != u_out; u = node[u].parent) {
            bfs[S++] = u;
        }
        for (i = S - 1; i >= 0; i--) {
            int u = bfs[i], p = node[u].parent;
            children.erase(p);
            children.push_back(u, p);
            node[p].parent = u;
            node[p].pred = node[u].pred;
        }
        children.erase(u_in);
        children.push_back(v_in, u_in);
        node[u_in].parent = v_in;
        node[u_in].pred = in_arc;

        CostSum current_pi = reduced_cost(in_arc);
        CostSum pi_delta = (u_in == edge[in_arc].node[0] ? -1 : +1) * current_pi;
        assert(pi_delta != 0);

        bfs[0] = u_in;
        for (i = 0, S = 1; i < S; i++) {
            int u = bfs[i];
            node[u].pi += pi_delta;
            FOR_EACH_IN_LINKED_LIST (v, u, children) { bfs[S++] = v; }
        }
    }
};

namespace std {
string to_string(__int128_t x) {
    if (x == 0)
        return "0";
    __uint128_t k = x;
    if (k == (((__uint128_t)1) << 127))
        return "-170141183460469231731687303715884105728";
    string result;
    if (x < 0) {
        result += "-";
        x *= -1;
    }
    string t;
    while (x) {
        t.push_back('0' + x % 10);
        x /= 10;
    }
    reverse(t.begin(), t.end());
    return result + t;
}

ostream& operator<<(ostream& o, __int128_t x) { return o << to_string(x); }
} // namespace std

#define INF 1e6

struct Specimen {
    int index;
    string tag, name, username, fullName;
    set<int> wishlist;
};

static map<string, Specimen> SpecimenList;
static map<int, string> SpecimenByIndex;

// Risk of stack overflow if input is too big
regex specimenRegex{"^([^\\s]+)\\s*==>\\s*(.+?)\\s*\\(from\\s+(.+?)---(.+?)\\)$"};
regex wishlistRegex{"^\\(([^)]+)---([^)]+)\\)\\s*([^:]+)\\s*:\\s*(.+)$"};

int ItemCount;

using namespace chrono;
class timer: high_resolution_clock {
    const time_point start_time;
public:
    timer(): start_time(now()) {}
    rep elapsed_time() const { return duration_cast<milliseconds>(now()-start_time).count(); }
};

int main() {
    timer T;
    freopen("input.txt", "r", stdin);
    freopen("output.txt", "w", stdout);

    string line;

    while (getline(cin, line)) {
        if(line.size() == 0) continue;
        if(line[0] == '#') continue;
        if(line == "!BEGIN-OFFICIAL-NAMES"){
            while (getline(cin, line) and line != "!END-OFFICIAL-NAMES") {
                smatch matches;

                regex_search(line, matches, specimenRegex);
                assert(SpecimenList.count(matches[1]) == 0);

                int elems = SpecimenList.size();
                SpecimenList[matches[1]] = {elems, matches[1], matches[2], matches[3], matches[4], {}};
                SpecimenByIndex[elems] = matches[1];
            }

            ItemCount = SpecimenList.size();
        } else {
            // Wishlist
            reverse(line.begin(), line.end());
            istringstream iss(line);
            string item;
            vector<string> wishlist;

            while ( iss >> item and item[0] != ':' ) {
                reverse(item.begin(), item.end());
                wishlist.push_back(item);
            }
            reverse(wishlist.begin(), wishlist.end());

            iss >> item;
            reverse(item.begin(), item.end());
            // item = leftmost main item tag 

            assert (SpecimenList.count(item) > 0 or item[0] == '%'); // Game declared before (or dummy)
            if(not SpecimenList.count(item)){
                assert(item[0] == '%');
                int elems = SpecimenList.size();
                SpecimenList[item].index = elems;
                SpecimenByIndex[elems] = SpecimenList[item].tag = item;
            }

            for(const auto &i : wishlist){
                if(not SpecimenList.count(i)){ // Non-declared wishlisted
                    assert(i[0] == '%'); // Should be a non-declared dummy
                    int elems = SpecimenList.size();
                    SpecimenList[i].index = elems;
                    SpecimenByIndex[elems] = SpecimenList[i].tag = i;
                }

                SpecimenList[i].wishlist.insert(SpecimenList[item].index);
            }
        }
    }

    cout << "Processed input after " << T.elapsed_time() << "ms" << endl;

    int origV = SpecimenList.size(), origE = 0;
    for(const auto& [key, s] : SpecimenList){
        origE += s.wishlist.size();
    }

    int V = origV * 2, E = origE + origV;

    network_simplex<long, long, __int128_t> ns(V);

    // Simplex supply / demand
    for (int v = 0; v < origV; v++) ns.add_supply(v, 1);
    for (int v = origV; v < V; v++) ns.add_supply(v, -1);

    vector<pair<int,int>> Edges;

    for(const auto& [key, specimen] : SpecimenList){ // Wishlists
        for(const auto& wishIndex : specimen.wishlist){
            Edges.push_back({specimen.index, wishIndex + origV});
            ns.add(specimen.index, wishIndex + origV, 0, 1, 1);
        }
    }

    for (int v = 0; v < origV; v++){ // Matching loop hack
        Edges.push_back({v, v + origV});
        ns.add(v, v + origV, 0, 1, INF);
    }

    if (!ns.mincost_circulation()) {
        cout << "Malformed graph -- Input error / Critical bug\n";

        return 0;
    }

    // cout << "Circulation cost = " << ns.get_circulation_cost() << '\n';
    int nondummy = 0;
    map<int,int> solution;
    for (int e = 0; e < origE; e++) {
        if(ns.get_flow(e)){
            assert(solution.count(Edges[e].first) == 0);
            solution[Edges[e].first] = Edges[e].second;
            if(SpecimenByIndex[Edges[e].first][0] != '%') nondummy++;
        }
    }

    cout << "Solution count with dummies: " << solution.size() << endl;
    cout << "Solution count without dummies: " << nondummy << endl;
    cout << "ItemCount = " << ItemCount << endl;
    cout << "origV = " << origV << ", V = " << V << endl;
    cout << "origE = " << origE << ", E = " << E << endl;

    map<int,int> clean;
    for(auto [key, val] : solution){
        if(key < ItemCount){ // Actual item to be sent
            int trueVal = val;
            
            while(trueVal - origV >= ItemCount){
                assert(solution.count(trueVal - origV) > 0);
                trueVal = solution[trueVal - origV]; // find last node 
            }

            assert(clean.count(key) == 0);
            if(key != trueVal - origV){ // Self loop => Non traded item
                clean[key] = trueVal - origV;
            }
        }
    }

    vector<vector<int>> groups;
    map<int,bool> visit;
    for(auto [key, val] : clean){
        if(not visit[key]){
            visit[key] = true;
            groups.push_back({key});
            int u = clean[key];
            while(u != key){
                visit[u] = true;
                groups.back().push_back(u);
                u = clean[u];
            }
        }
    }
    // sort(groups.begin(), groups.end(), greater<>());


    cout << "Clean up solution count: " << clean.size() << endl;
    cout << "Groups: "; for(auto& g : groups) cout << g.size() << ' '; cout << endl;
    cout << "Elapsed time: " << T.elapsed_time() << "ms" << endl;

    cout << "Trades: " << endl;
    for(auto [key, val] : clean){
        Specimen _left = SpecimenList[SpecimenByIndex[key]];
        Specimen _right = SpecimenList[SpecimenByIndex[val]];

        cout << "(" << _left.name << " " << _left.username << "---" << _left.fullName <<  ") " << _left.tag << "\t ==> " ;
        cout << "(" << _right.name << " " << _right.username << "---" << _right.fullName <<  ") " << _right.tag << "\n";
    }

    return 0;
}
