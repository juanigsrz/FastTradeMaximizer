#pragma GCC optimize("O3")
#pragma GCC optimize("unroll-loops")
// #pragma GCC target("avx,avx2,sse,sse2,sse3,sse4,popcnt")

#include <bits/stdc++.h>
#include "network_simplex.hpp"
#include "chrono.hpp"

#define REQUIRE_COLON true
#define REQUIRE_OFFICIAL_NAMES false
#define REQUIRE_USERNAMES true
#define ITERATIONS 100
#define SHOW_TRADES false
#define SEED CLOCK_RNG

using namespace std;
using ll = long long;


ll INF = 1e9;

struct Specimen {
    int index;
    string username;
    vector<int> wishlist;
    bool dummy;
};

mt19937_64 rng(SEED);
unordered_map<string, Specimen> Items;
unordered_map<int, string> Tags;

void sccShrinkOptimization(){
    vector<vector<int>> adj(2 * Items.size()), adj_rev(2 * Items.size());
    for(const auto& [key, s] : Items){
        for(auto w : s.wishlist){
            adj[s.index].push_back(w);
            adj_rev[w].push_back(s.index);
        }
    }
    vector<int> order;
    set<int> component;
    vector<bool> used;
    function<void(int)> dfs1 = [&](int v){ used[v] = true; for(auto& u : adj[v]) if(not used[u]) dfs1(u); order.push_back(v); };
    function<void(int)> dfs2 = [&](int v){ used[v] = true; component.insert(v); for(auto& u : adj_rev[v]) if(not used[u]) dfs2(u); };
    used.assign(2 * Items.size(), false);
    for(int i = 0; i < 2 * Items.size(); i++) if(not used[i]) dfs1(i);
    used.assign(2 * Items.size(), false);
    reverse(order.begin(), order.end());

    int optimizedEdges = 0;
    for (auto v : order) if (not used[v]) {
        dfs2(v);

        for(auto x : component){
            if(component.size() == 1){ Items.erase(Tags[x]); continue; } 
            Specimen& s = Items[Tags[x]];

            vector<int> temp;
            for(auto w : s.wishlist){
                if(component.count(w)){ // Only keep edges within the SCC
                    temp.push_back(w);
                } else {
                    optimizedEdges++;
                }
            }
            s.wishlist = temp;
        }
        component.clear();
    }

    set<int> deletedOrphans;
    for(auto it = Items.begin(); it != Items.end(); ){ // Delete oprhans (nodes left without outgoing edges)
        if((it->second).wishlist.empty()){
            deletedOrphans.insert((it->second).index);
            it = Items.erase(it);
        }
        else it++;
    }

    // Re-index nodes
    int total = 0;
    unordered_map<int, int> mapping;
    for(auto &[tag, item] : Items){
        if(not mapping.count(item.index)){
            mapping[item.index] = mapping.size();

            for(auto &w : item.wishlist){
                if(not mapping.count(w) and not deletedOrphans.count(w)){
                    mapping[w] = mapping.size();
                }
            }
        }
    }

    unordered_map<int, string> newTags;
    for(auto &[tag, item] : Items){
        item.index = mapping[item.index];
        newTags[item.index] = tag;

        vector<int> temp;
        for(auto &w : item.wishlist){
            if(mapping.count(w)){
                assert(mapping[w] < Items.size());
                temp.push_back(mapping[w]);
            }
        }
        item.wishlist = temp;
    }
    Tags = newTags;
}

int solve(){
    network_simplex<ll, ll, __int128_t> ns(2 * Items.size());

    // Simplex supply / demand
    for (int v = 0; v < Items.size(); v++){
        ns.add_supply(v, 1);
        ns.add_supply(v + Items.size(), -1);
    } 

    vector<pair<int,int>> Edges;

    // Patch to quickly shuffle results
    vector<string> randTag;
    for(const auto& [key, val] : Items) randTag.push_back(key);
    shuffle(randTag.begin(), randTag.end(), rng);

    for(const auto& tag : randTag){ // Build edges from wishlists
        Specimen specimen = Items[tag];
        shuffle(specimen.wishlist.begin(), specimen.wishlist.end(), rng);

        for(const auto& wishIndex : specimen.wishlist){
            assert(specimen.index != wishIndex);
            Edges.push_back({specimen.index, wishIndex + Items.size()});

            /* Cost could possibly be changed to 0 and INF to 1 */
            ll cost = 1;

            if(specimen.dummy) cost = INF;
            ns.add(specimen.index, wishIndex + Items.size(), 0, 1, cost); 
        }
    }

    for (int v = 0; v < Items.size(); v++){ // Matching loop idea hack
        Edges.push_back({v, v + Items.size()});
        ns.add(v, v + Items.size(), 0, 1, INF);
    }

    if (ns.mincost_circulation() == 0) { // Run simplex
        cout << "Ill-formed graph -- Input error / Critical bug\n";
        return -1;
    }

    map<int, int> solution;
    for (int e = 0; e < Edges.size(); e++) {
        if(ns.get_flow(e)){
            assert(solution.count(Edges[e].first) == 0);
            solution[Edges[e].first] = Edges[e].second;
            if(not Tags[Edges[e].first].size() > 0){
                cout << "WHOOPSIE -> " << Edges[e].first << endl;
            }
            assert(Tags[Edges[e].first].size() > 0);
        }
    }

    // cout << "Solution count with dummy trades: " << solution.size() << endl;
    // cout << "Item count with dummies = " << Items.size() << endl;

    map<int,int> clean;
    for(auto [key, val] : solution){
        assert(Tags.count(key) > 0);
        if(not Items[Tags[key]].dummy){ // Actual item to be sent
            int trueVal = val;

            while(Items[Tags[trueVal - Items.size()]].dummy){
                trueVal = solution[trueVal - Items.size()];
            }

            assert(clean.count(key) == 0);
            if(key != trueVal - Items.size()){ // Self loop implies a non traded item
                clean[key] = trueVal - Items.size();
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
    // sort(groups.begin(), groups.end(), [](const vector<int>& a, const vector<int>& b){ return a.size() > b.size(); }); // Format in group-size decreasing order


    // cout << "Clean up solution count: " << clean.size() << '\n';
    /* // Watch out of concurrency
    cout << "Total trades = " << clean.size() << '\n';
    cout << "" << groups.size() << "): "; for(auto& g : groups) cout << g.size() << ' '; cout << '\n';
    */

    if(SHOW_TRADES) cout << "Trades: " << endl;

    set<string> TradingUsers;
    for(auto &g : groups){
        for(int i = 0; i < g.size(); i++){
            const Specimen& _left = Items[Tags[g[i]]];
            const Specimen& _right = Items[Tags[g[(i+1)%g.size()]]];

            if(SHOW_TRADES){
                if(REQUIRE_USERNAMES) cout << "(" << _left.username << ") ";
                cout << Tags[g[i]] << "\t ==> " ;
                if(REQUIRE_USERNAMES) cout << "(" << _right.username << ") ";
                cout << Tags[g[(i+1)%g.size()]] << '\n';
            }

            TradingUsers.insert(_left.username);
            TradingUsers.insert(_right.username);
        }
        if(SHOW_TRADES) cout << '\n';
    }
    return TradingUsers.size();
}

int main() {
    timer T;

    // cin.tie(NULL)->sync_with_stdio(false);
    freopen("input.txt", "r", stdin);
    freopen("output.txt", "w", stdout);

    string line;

    while (getline(cin, line)) {
        if(line.size() == 0) continue;
        if(line[0] == '#') continue;
        if(line == "!BEGIN-OFFICIAL-NAMES"){
            while (getline(cin, line) and line != "!END-OFFICIAL-NAMES") {
                istringstream iss(line);
                string tag;

                iss >> tag;
                
                assert(Items.count(tag) == 0); // Repeated tag in official names

                int elems = Items.size();
                Items[tag] = Specimen{
                    .index = elems,
                    .dummy = false
                };
                Tags[elems] = tag;
            }
        } else {
            // Wishlists
            istringstream iss(line);
            string temp, username;

            if(REQUIRE_USERNAMES){
                getline(iss, temp, '(');
                getline(iss, username, ')');
                assert(temp.size() == 0 and username.size() > 0);
            }
            
            string tag;
            iss >> tag;

            if(REQUIRE_COLON){
                if(tag.back() == ':'){
                    tag.pop_back();
                } else {
                    getline(iss, temp, ':');
                    /* beautify */
                }
            }

            if(tag[0] == '%') tag += username;
            if(not Items.count(tag)){
                if(REQUIRE_OFFICIAL_NAMES) assert(tag[0] == '%'); // Must be a dummy not seen before
                int elems = Items.size();
                Items[tag].index = elems;
                Tags[elems] = tag;
                Items[tag].dummy = tag[0] == '%';
            }
            
            if(Items[tag].username == ""){
                Items[tag].username = username;
            }
            else assert (Items[tag].username == username);

            while(iss >> temp){
                if(temp[0] == '%') temp += username;
                if(not Items.count(temp)){
                    if(REQUIRE_OFFICIAL_NAMES) assert(temp[0] == '%'); // Must be a dummy not seen before
                    int elems = Items.size();
                    Items[temp].index = elems;
                    Tags[elems] = temp;
                    Items[temp].dummy = temp[0] == '%';
                }

                Items[temp].wishlist.push_back(Items[tag].index);
            }
        }
    }

    cout << "Processed input after " << T.elapsed_time() << "ms" << endl;

    // SCC
    sccShrinkOptimization();


    int maxUsers = 0;
    vector<future<int>> Run(ITERATIONS);
    for(int i = 0; i < ITERATIONS; i++) Run[i] = async(&solve);
    for(int i = 0; i < ITERATIONS; i++) {
        int result = Run[i].get();
        assert(result != -1);
        maxUsers = max(result, maxUsers);
        cout << "Trading with " << result << " users (max found = " << maxUsers << ")" << '\n';
    }

    /*
    for(int _i = 0; _i < ITERATIONS; _i++){
        vector<thread> T(4);
        for(int t = 0; t < T.size(); t++) T[t] = thread(run);
        for(int t = 0; t < T.size(); t++) T[t].join();

        
        int result = run();
        assert(result != -1);
        maxUsers = max(result, maxUsers);
        cout << "Trading with " << result << " users (max found = " << maxUsers << ")" << '\n';
    }
    */
    cout << "Elapsed time: " << T.elapsed_time() << "ms" << '\n';

    return 0;
}
