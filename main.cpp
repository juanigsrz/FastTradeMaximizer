#pragma GCC optimize("O3")
#pragma GCC optimize("unroll-loops")
// #pragma GCC target("avx,avx2,sse,sse2,sse3,sse4,popcnt")

#include <bits/stdc++.h>
#include "network_simplex.hpp"
#include "chrono.hpp"

#define REQUIRE_COLON true
#define REQUIRE_OFFICIAL_NAMES false
#define REQUIRE_USERNAMES true
#define ITERATIONS 1
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
static unordered_map<string, Specimen> Items;
static unordered_map<int, string> indexToTag;


int main() {
    timer T;

    cin.tie(NULL)->sync_with_stdio(false);
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
                indexToTag[elems] = tag;
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
                indexToTag[elems] = tag;
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
                    indexToTag[elems] = temp;
                    Items[temp].dummy = temp[0] == '%';
                }

                Items[temp].wishlist.push_back(Items[tag].index);
            }
        }
    }

    cout << "Processed input after " << T.elapsed_time() << "ms" << endl;

    int origV = Items.size(), origE = 0;
    for(const auto& [key, s] : Items){
        origE += s.wishlist.size();
    }

    // SCC
    vector<vector<int>> adj(2 * origV), adj_rev(2 * origV);
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
    used.assign(2 * origV, false);
    for(int i = 0; i < 2 * origV; i++) if(not used[i]) dfs1(i);
    used.assign(2 * origV, false);
    reverse(order.begin(), order.end());

    for (auto v : order) if (not used[v]) {
        dfs2(v);

        for(auto x : component){
            if(component.size() == 1){ Items.erase(indexToTag[x]); continue; } 
            Specimen& s = Items[indexToTag[x]];

            vector<int> newWishlist;
            for(auto w : s.wishlist){
                if(component.count(w)){ // Only keep edges within SCC
                    newWishlist.push_back(w);
                }
            }
            s.wishlist = newWishlist;
        }
        component.clear();
    }

    for(auto it = Items.begin(); it != Items.end(); ){ // Delete oprhans
        if((it->second).wishlist.empty()) it = Items.erase(it);
        else it++;
    }


    int maxUsers = 0;
    for(int _i = 0; _i < ITERATIONS; _i++){
        network_simplex<ll, ll, __int128_t> ns(2 * origV);

        // Simplex supply / demand
        for (int v = 0; v < origV; v++){
            ns.add_supply(v, 1);
            ns.add_supply(v + origV, -1);
        } 

        vector<pair<int,int>> Edges;

        // Patch to quickly shuffle results
        vector<string> randTag(Items.size());
        for(const auto& [key, val] : Items) randTag.push_back(key);
        shuffle(randTag.begin(), randTag.end(), rng);

        for(const auto& tag : randTag){ // Wishlists
            Specimen specimen = Items[tag];
            shuffle(specimen.wishlist.begin(), specimen.wishlist.end(), rng);

            for(const auto& wishIndex : specimen.wishlist){
                assert(specimen.index != wishIndex);
                Edges.push_back({specimen.index, wishIndex + origV});

                /* Cost could possibly be changed to 0 and INF to 1 */
                ll cost = 1;

                if(specimen.dummy) cost = INF;
                ns.add(specimen.index, wishIndex + origV, 0, 1, cost); 
            }
        }

        for (int v = 0; v < origV; v++){ // Matching loop hack
            Edges.push_back({v, v + origV});
            ns.add(v, v + origV, 0, 1, INF);
        }

        if (ns.mincost_circulation() == 0) {
            cout << "Ill-formed graph -- Input error / Critical bug\n";

            return 0;
        }

        map<int, int> solution;
        for (int e = 0; e < Edges.size(); e++) {
            if(ns.get_flow(e)){
                assert(solution.count(Edges[e].first) == 0);
                solution[Edges[e].first] = Edges[e].second;
                assert(indexToTag[Edges[e].first].size() > 0);
            }
        }

        // cout << "Solution count with dummy trades: " << solution.size() << endl;
        // cout << "Item count with dummies = " << Items.size() << endl;

        map<int,int> clean;
        for(auto [key, val] : solution){
            assert(indexToTag.count(key) > 0);
            if(not Items[indexToTag[key]].dummy){ // Actual item to be sent
                int trueVal = val;

                while(Items[indexToTag[trueVal - origV]].dummy){
                    trueVal = solution[trueVal - origV];
                }

                assert(clean.count(key) == 0);
                if(key != trueVal - origV){ // Self loop implies a non traded item
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
        // sort(groups.begin(), groups.end(), [](const vector<int>& a, const vector<int>& b){ return a.size() > b.size(); }); // Format in group-size decreasing order


        // cout << "Clean up solution count: " << clean.size() << '\n';
        cout << "Groups (" << groups.size() << "): "; for(auto& g : groups) cout << g.size() << ' '; cout << '\n';

        if(SHOW_TRADES) cout << "Trades: " << endl;

        set<string> TradingUsers;
        for(auto &g : groups){
            for(int i = 0; i < g.size(); i++){
                const Specimen& _left = Items[indexToTag[g[i]]];
                const Specimen& _right = Items[indexToTag[g[(i+1)%g.size()]]];
    
                if(SHOW_TRADES){
                    if(REQUIRE_USERNAMES) cout << "(" << _left.username << ") ";
                    cout << indexToTag[g[i]] << "\t ==> " ;
                    if(REQUIRE_USERNAMES) cout << "(" << _right.username << ") ";
                    cout << indexToTag[g[(i+1)%g.size()]] << '\n';
                }

                TradingUsers.insert(_left.username);
                TradingUsers.insert(_right.username);
            }
            if(SHOW_TRADES) cout << '\n';
        }
        maxUsers = max(maxUsers, (int) TradingUsers.size());
        cout << "Trading with " << TradingUsers.size() << " users (max found = " << maxUsers << ")" << '\n';
    }
    cout << "Elapsed time: " << T.elapsed_time() << "ms" << '\n';

    return 0;
}
