#pragma GCC optimize("O3")
#pragma GCC optimize("unroll-loops")
// #pragma GCC target("avx,avx2,sse,sse2,sse3,sse4,popcnt") /* might go faster at the expense of breaking Windows builds */

#include <bits/stdc++.h>
#include "network_simplex.hpp"
#include "chrono.hpp"

using namespace std;
using ll = long long;


static class Config {
public:
    enum METRIC_TYPE { USERS_TRADING = 0 } METRIC;
    bool ALLOW_DUMMIES , REQUIRE_COLONS, REQUIRE_USERNAMES, REQUIRE_OFFICIAL_NAMES,
        SHOW_MISSING, SHOW_WANTS, SHOW_TRADES, SHOW_ELAPSED_TIME,
        HIDE_LOOPS, HIDE_SUMMARY, HIDE_NONTRADES, HIDE_ERRORS, HIDE_REPEATS, HIDE_STATS,
        SORT_BY_ITEM, CASE_SENSITIVE, VERBOSE, SHRINK_VERBOSE;

    ll SMALL_STEP = 0, BIG_STEP = 0, ITERATIONS = 1, SEED, NONTRADE_COST = 1e9, SHRINK = 0;
} Settings;

// Real items and dummies are uniquely mapped to an integer from 0 to N, being N the total number.
// When separating into "Sender" and "Receiver" nodes, an index I in [0,N) represents the Ith item's "Sender", 
// and index I + N represent the Ith item's "Receiver". So you can add or substract N to match some item index to its pair.
struct Specimen {
    int index;
    string username;
    vector<int> wishlist;
    bool dummy;
};

mt19937_64 rng;
unordered_map<string, Specimen> Items; // Maps tags to the corresponding item
unordered_map<int, string> Tags; // Maps indices to tags, basically to represent a "bidirectional map"


// See Chris Okasaki explanation: https://boardgamegeek.com/thread/1601921/math-trade-theory-classifying-edges
// sccShrinkOptimization() implements Kosaraju to find each SCC (strongly conected component) and then
// erase every outgoing edge from this component to a different one (particularly, not the same component).
// After that it also erases nodes that have been left without edges, "orphans".
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


// solve() runs the whole sauce of solving the math trade.
// It's been repurposed to be ran concurrently by many threads, though this function reads and doesn't write concurrent
// data structures (i.e. Items, Tags), which is thread safe.
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

            ll cost = 1;

            if(specimen.dummy) cost = Settings.NONTRADE_COST;
            ns.add(specimen.index, wishIndex + Items.size(), 0, 1, cost); 
        }
    }

    for (int v = 0; v < Items.size(); v++){ // Self-matching loop idea
        Edges.push_back({v, v + Items.size()});
        ns.add(v, v + Items.size(), 0, 1, Settings.NONTRADE_COST);
    }

    if (ns.mincost_circulation() == 0) { // Run simplex
        cout << "Ill-formed graph -- Input error / Critical bug\n";
        return -1;
    }

    map<int, int> solution; // Solution in the abstracted space of Senders and Receivers
    for (int e = 0; e < Edges.size(); e++) {
        if(ns.get_flow(e)){
            assert(solution.count(Edges[e].first) == 0);
            solution[Edges[e].first] = Edges[e].second;
            assert(Tags[Edges[e].first].size() > 0);
        }
    }

    // cout << "Solution count with dummy trades: " << solution.size() << endl;
    // cout << "Item count with dummies = " << Items.size() << endl;

    map<int,int> clean; // Cleaned up solution with indices back to [0,N)
    for(auto [key, val] : solution){
        assert(Tags.count(key) > 0);
        if(not Items[Tags[key]].dummy){ // Actual item to be sent
            int trueVal = val;

            while(Items[Tags[trueVal - Items.size()]].dummy){ // Skips trades between dummies
                trueVal = solution[trueVal - Items.size()];
            }

            assert(clean.count(key) == 0);
            if(key != trueVal - Items.size()){ // Self loop implies a non traded item
                clean[key] = trueVal - Items.size();
            }
        }
    }

    vector<vector<int>> groups; // Trading chains, or "groups"
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

    if(Settings.SHOW_TRADES) cout << "Trades: " << endl;

    set<string> TradingUsers;
    for(auto &g : groups){
        for(int i = 0; i < g.size(); i++){
            const Specimen& _left = Items[Tags[g[i]]];
            const Specimen& _right = Items[Tags[g[(i+1)%g.size()]]];

            if(Settings.SHOW_TRADES){
                if(Settings.REQUIRE_USERNAMES) cout << "(" << _left.username << ") ";
                cout << Tags[g[i]] << "\t ==> " ;
                if(Settings.REQUIRE_USERNAMES) cout << "(" << _right.username << ") ";
                cout << Tags[g[(i+1)%g.size()]] << '\n';
            }

            TradingUsers.insert(_left.username);
            TradingUsers.insert(_right.username);
        }
        if(Settings.SHOW_TRADES) cout << '\n';
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
        if(line[0] == '#'){
            if(line.size() <= 1 or line[1] != '!') continue; // Comment
            // Option
            istringstream iss(line);
            string option;
            iss >> option; // Discard initial "#!" stream tokens

            while(iss >> option){
                bool validOption = true;

                if(option == "ALLOW-DUMMIES")               Settings.ALLOW_DUMMIES = true;
                else if(option == "REQUIRE-COLONS")         Settings.REQUIRE_COLONS = true;
                else if(option == "REQUIRE-USERNAMES")      Settings.REQUIRE_USERNAMES = true;
                else if(option == "REQUIRE-OFFICIAL-NAMES") Settings.REQUIRE_OFFICIAL_NAMES = true;
                else if(option == "SHOW-MISSING")           Settings.SHOW_MISSING = true;
                else if(option == "SHOW-WANTS")             Settings.SHOW_WANTS = true;
                else if(option == "SHOW-TRADES")            Settings.SHOW_TRADES = true;
                else if(option == "SHOW-ELAPSED-TIME")      Settings.SHOW_ELAPSED_TIME = true;
                else if(option == "HIDE-LOOPS")             Settings.HIDE_LOOPS = true;
                else if(option == "HIDE-SUMMARY")           Settings.HIDE_SUMMARY = true;
                else if(option == "HIDE-NONTRADES")         Settings.HIDE_NONTRADES = true;
                else if(option == "HIDE-ERRORS")            Settings.HIDE_ERRORS = true;
                else if(option == "HIDE-REPEATS")           Settings.HIDE_REPEATS = true;
                else if(option == "HIDE-STATS")             Settings.HIDE_STATS = true;
                else if(option == "SORT-BY_ITEM")           Settings.SORT_BY_ITEM = true;
                else if(option == "CASE-SENSITIVE")         Settings.CASE_SENSITIVE = true;
                else if(option == "VERBOSE")                Settings.VERBOSE = true;
                else if(option == "SHRINK-VERBOSE")         Settings.SHRINK_VERBOSE = true;
                else if(option.find('=') != string::npos and 0 < option.find('=') and option.find('=') < option.size() - 1){
                    // Must be "Option=Value"
                    string name = option.substr(0, option.find('='));
                    string value = option.substr(option.find('=') + 1);

                    if(name == "SMALL-STEP")        Settings.SMALL_STEP = stoll(value);
                    else if(name == "BIG-STEP")     Settings.BIG_STEP = stoll(value);
                    else if(name == "ITERATIONS")   Settings.ITERATIONS = stoll(value);
                    else if(name == "SEED")         Settings.SEED = stoll(value); // TODO : Figure out why not repeatable (guess is mt19937_64)
                    else if(name == "SHRINK")       Settings.SHRINK = stoll(value);
                    else if(name == "METRIC")       Settings.METRIC = Config::USERS_TRADING; // There is no other METRIC
                    else validOption = false;
                } else validOption = false;

                if(not validOption) {
                    cout << "WARNING: Ignoring non-supported or wrongly typed option \"" << option << "\".\n";
                }
            }
        }
        else if(line == "!BEGIN-OFFICIAL-NAMES"){
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
            if(Settings.REQUIRE_OFFICIAL_NAMES) assert(Items.size() > 0); // Cannot wishlist without listing official names
            istringstream iss(line);
            string temp, username;

            if(Settings.REQUIRE_USERNAMES){
                getline(iss, temp, '(');
                getline(iss, username, ')');
                assert(temp.size() == 0 and username.size() > 0);
            }
            
            string tag;
            iss >> tag;

            if(Settings.REQUIRE_COLONS){
                if(tag.back() == ':'){
                    tag.pop_back();
                } else {
                    getline(iss, temp, ':');
                    /* beautify */
                }
            }

            if(tag[0] == '%') tag += username;
            if(not Items.count(tag)){
                if(Settings.REQUIRE_OFFICIAL_NAMES) assert(tag[0] == '%'); // Must be a dummy not seen before
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
                    if(Settings.REQUIRE_OFFICIAL_NAMES) assert(temp[0] == '%'); // Must be a dummy not seen before
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

    rng = mt19937_64(Settings.SEED);

    // SCC
    sccShrinkOptimization();


    int maxUsers = 0;
    vector<future<int>> Run(Settings.ITERATIONS);
    for(int i = 0; i < Settings.ITERATIONS; i++) Run[i] = async(&solve);
    for(int i = 0; i < Settings.ITERATIONS; i++) {
        int result = Run[i].get();
        assert(result != -1);
        maxUsers = max(result, maxUsers);
        cout << "Trading with " << result << " users (max found = " << maxUsers << ")" << '\n';
    }

    /*
    for(int _i = 0; _i < _ITERATIONS; _i++){
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
