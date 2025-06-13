#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <map>
#include <iomanip>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "gurobi_c++.h"

#include "utils.hpp"
#include "md5.hpp"

using namespace std;
using ll = long long;

static class Stats {
public:
    int totalRealItems = 0, tradedItems = 0;
    ll  sumSquares = 0, trackedMetric = 0, sameProvinceTrades = 0;
    size_t formattingWidth = 0;

    string commandLine, inputChecksum, resultsChecksum;
    vector<string> options, customOutput;
    struct { int optimizedRealitems, deletedOrphans; } shrinked;

    utils::timer Timer;
    chrono::time_point<chrono::system_clock> startTime;
    const string version = "0.5";
} Metadata;

static class Config {
public:
    enum METRIC_TYPE { USERS_TRADING = 0 } METRIC;
    bool ALLOW_DUMMIES , REQUIRE_COLONS, REQUIRE_USERNAMES, REQUIRE_OFFICIAL_NAMES,
        SHOW_MISSING, SHOW_WANTS, SHOW_ELAPSED_TIME,
        HIDE_LOOPS, HIDE_SUMMARY, HIDE_NONTRADES, HIDE_ERRORS, HIDE_REPEATS, HIDE_STATS,
        SORT_BY_ITEM, CASE_SENSITIVE, VERBOSE;

    ll SMALL_STEP = 0, BIG_STEP = 0, ITERATIONS = 1, SEED, NONTRADE_COST = 1e12, SHRINK = 0;
} Settings;

// Real items and dummies are uniquely mapped to an integer from 0 to N, being N the total number.
// When separating into "Sender" and "Receiver" nodes, an index I in [0,N) represents the Ith item's "Sender", 
// and index I + N represent the Ith item's "Receiver". So you can add or substract N to match some item index to its pair.
struct Specimen {
    int index;
    string tag, username;
    vector<int> wishlist;
    bool dummy;

    string show() const {
        if(Settings.SORT_BY_ITEM) return this->tag + " " + "(" + this->username + ")";
        else                      return "(" + this->username + ")" + " " + this->tag;
    }
};


unordered_map<string, Specimen> Items; // Maps tags to the corresponding item
unordered_map<int, string> Tags; // Maps indices to tags, basically to represent a "bidirectional map"
vector<string> Provinces = {"Buenos Aires", "Catamarca", "Chaco", "Chubut", "Córdoba", "Corrientes", "Entre Ríos", "Formosa", "Jujuy", "La Pampa", "La Rioja", "Mendoza", "Misiones", "Neuquén", "Río Negro", "Salta", "San Juan", "San Luis", "Santa Cruz", "Santa Fe", "Santiago del Estero", "Tierra del Fuego", "Tucumán", "Ciudad Autónoma de Buenos Aires"};
unordered_map<string, string> Province; // Example of maximizing intra-state trades to minimize shipping costs


// solve_gurobi() runs the gurobi solver
vector<vector<int>> bestGroups;
bool solve_gurobi() {
    try {
        // Build the list of edges
        std::vector<std::pair<int, int>> Edges;
        for (const auto& [tag, s] : Items) {
            for (const auto& sendTo : s.wishlist) {
                assert(s.index != sendTo);
                Edges.emplace_back(s.index, sendTo);
            }
        }

        // Initialize Gurobi
        GRBEnv env = GRBEnv(true);
        env.set(GRB_IntParam_OutputFlag, 1); // Set to 0 to suppress output
        env.start();
        GRBModel model = GRBModel(env);

        // Map from username to their items
        std::unordered_map<std::string, std::set<int>> userItems;
        for (const auto& [tag, s] : Items) {
            if(!s.dummy){
                userItems[s.username].insert(s.index);

                if(!Province.count(s.username)){
                    // Pick one at random
                    Province[s.username] = Provinces[(s.index % Provinces.size() + Provinces.size()) % Provinces.size()];
                }
            }
        }

        // Binary variables for trades
        std::vector<GRBVar> x(Edges.size());
        for (size_t k = 0; k < Edges.size(); ++k) {
            x[k] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY);
        }

        // Binary variables for user participation
        std::unordered_map<std::string, GRBVar> y_user;
        for (const auto& [user, items] : userItems) {
            y_user[user] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY);
        }

        // Link user participation to involvement in a trade
        for (const auto& [user, items] : userItems) {
            GRBLinExpr userExpr;
            
            for (int item : items) {
                for (size_t k = 0; k < Edges.size(); ++k) {
                    if (Edges[k].first == item || Edges[k].second == item) {
                        userExpr += x[k];
                    }
                }
            }

            model.addConstr(y_user[user] <= userExpr);
        }

        // Flow constraints: in-degree == out-degree <= 1
        for (size_t i = 0; i < Items.size(); ++i) {
            GRBLinExpr inflow, outflow;
            
            for (size_t k = 0; k < Edges.size(); ++k) {
                if (Edges[k].first == i) outflow += x[k];
                if (Edges[k].second == i) inflow += x[k];
            }

            model.addConstr(inflow <= 1);
            model.addConstr(inflow == outflow);
        }

        // Objective 1: Maximize number of trades
        GRBLinExpr tradeObjective;
        for (int i = 0; i < Edges.size(); i++){
            if(Items[Tags[Edges[i].second]].dummy) continue;

            tradeObjective += x[i];
        }
        model.set(GRB_IntAttr_ModelSense, GRB_MAXIMIZE);
        model.setObjectiveN(tradeObjective, 0, 2); // Higher priority

        // Objective 2: Maximize number of users participating
        GRBLinExpr userObjective;
        for (const auto& [user, y] : y_user){
            userObjective += y;
        }
        model.setObjectiveN(userObjective, 1, 1); // 2nd lower priority

        // Objective 3: Maximize trades within the same Province
        GRBLinExpr sameProvinceObjective;
        for (size_t k = 0; k < Edges.size(); ++k) {
            const auto& fromItem = Items[Tags[Edges[k].first]];
            const auto& toItem   = Items[Tags[Edges[k].second]];

            if (Province[fromItem.username] == Province[toItem.username]) {
                sameProvinceObjective += x[k];
            }
        }
        model.setObjectiveN(sameProvinceObjective, 2, 0); // 3rd lower priority

        // Solve
        model.optimize();

        if (model.get(GRB_IntAttr_Status) != GRB_OPTIMAL) {
            std::cout << "No optimal solution found. Status: " 
                      << model.get(GRB_IntAttr_Status) << std::endl;
            return false;
        }

        map<int, int> solution; // Solution in the abstracted space of Senders and Receivers
        for (int e = 0; e < Edges.size(); e++) {
            if(x[e].get(GRB_DoubleAttr_X) > 0.5){
                assert(solution.count(Edges[e].first) == 0);
                solution[Edges[e].first] = Edges[e].second;
                assert(Tags[Edges[e].first].size() > 0);
            }
        }

        map<int,int> clean; // Cleaned up solution with indices back to [0,N)
        for(auto [key, val] : solution){
            assert(Tags.count(key) > 0);
            if(not Items[Tags[key]].dummy){ // Actual item to be sent
                while(Items[Tags[val]].dummy){ // Skips trades between dummies
                    val = solution[val];
                }

                assert(clean.count(key) == 0);
                clean[key] = val;
            }
        }

        for(const auto& [i, j] : clean){
            const auto& fromItem = Items[Tags[i]];
            const auto& toItem   = Items[Tags[j]];

            if (Province[fromItem.username] == Province[toItem.username]) {
                Metadata.sameProvinceTrades += 1;
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
        bestGroups = groups;

        unordered_set<string> TradingUsers;
        for (const auto& [user, y] : y_user) {
            if (y.get(GRB_DoubleAttr_X) > 0.5) {
                TradingUsers.insert(user);
            }
        }

        Metadata.trackedMetric = TradingUsers.size();

        return true;
    } catch (GRBException& e) {
        std::cerr << "Gurobi Error: " << e.getMessage() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error in Gurobi model." << std::endl;
    }

    return false;
}


void formatOutput(ostream& out){
    out << "FastTradeMaximizer Version " << Metadata.version << '\n';
    time_t startTimeT = chrono::system_clock::to_time_t(Metadata.startTime);
    out << "... run started: " << ctime(&startTimeT);
    out << "... command line: " << Metadata.commandLine << '\n';
    for(const auto& cO : Metadata.customOutput) cout << cO << '\n';
    out << "Options: "; for(const auto &o : Metadata.options) out << o << ' ';
    out << "\n\n";
    out << "Input Checksum: " << Metadata.inputChecksum << '\n';
    out << "Weeded down number of items: " << Metadata.shrinked.optimizedRealitems << " (" << Metadata.shrinked.deletedOrphans << " orphans)";
    out << "\n\n";
    out << "TRADE LOOPS (" << Metadata.tradedItems << " total trades):\n\n";

    sort(bestGroups.begin(), bestGroups.end(), [](const vector<int>& a, const vector<int>& b){ return a.size() > b.size(); }); // Format in group-size decreasing order

    Metadata.resultsChecksum = md5("");
    vector<string> itemSummary;
    for(const auto &g : bestGroups){
        for(int i = 0; i < g.size(); i++){
            // Generate trade loops
            const Specimen& current     = Items[Tags[g[i]]];
            const Specimen& sendTo      = Items[Tags[g[(i+1)%g.size()]]];
            const Specimen& receiveFrom = Items[Tags[g[(i-1+g.size())%g.size()]]];

            Metadata.resultsChecksum = md5(Metadata.resultsChecksum + sendTo.show() + current.show());
            out << std::left << setfill(' ') << setw(Metadata.formattingWidth) << sendTo.show() << " receives " << current.show() << '\n';

            // Prepare item summaries
            stringstream buffer;
            buffer  << std::left << setfill(' ') << setw(Metadata.formattingWidth) << current.show() << " receives "
                    << std::left << setfill(' ') << setw(Metadata.formattingWidth) << receiveFrom.show() << "and sends to "
                    << sendTo.show();
            itemSummary.push_back(buffer.str());
        }
        out << '\n';
    }

    out << "ITEM SUMMARY (" << Metadata.tradedItems << " total trades):\n\n";
    sort(itemSummary.begin(), itemSummary.end());
    for(const auto &s : itemSummary) out << s << '\n';

    out << "\n\n";
    out << "Results Checksum: " << Metadata.resultsChecksum << "\n\n";
    out << "Num trades        = " << Metadata.tradedItems << " of " << Metadata.totalRealItems << " items (" << Metadata.tradedItems * 100.0 / Metadata.totalRealItems * 1.0 << "%)\n";
    out << "Trading users     = " << Metadata.trackedMetric << '\n';
    out << "Same-state trades = " << Metadata.sameProvinceTrades << '\n';
    out << "Total cost        = " << Metadata.tradedItems << " (avg 1.00)\n"; // There is no priority implemented
    out << "Num groups        = " << bestGroups.size() << '\n';
    out << "Group sizes       = "; for(const auto& g : bestGroups) out << g.size() << ' '; out << '\n';
    out << "Sum squares       = " << Metadata.sumSquares << '\n';
    if(Settings.SHOW_ELAPSED_TIME) out << "Elapsed time = " << Metadata.Timer.elapsed_time() << "ms" << '\n';
}

int main(int argc, char** argv) {
    Metadata.startTime = chrono::system_clock::now();
    Metadata.commandLine = argv[0];

    string line;
    while (getline(cin, line)) {
        if(line.size() == 0) continue;
        if(line[0] == '#'){
            if(line[1] == '+'){ Metadata.customOutput.push_back(line); continue; } // Custom output
            if(line[1] != '!') continue; // Comment

            // Option
            Metadata.inputChecksum = md5(Metadata.inputChecksum + line);
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
                else if(option.find('=') != string::npos && 0 < option.find('=') && option.find('=') < option.size() - 1){
                    // Must be "Option=Value"
                    string name = option.substr(0, option.find('='));
                    string value = option.substr(option.find('=') + 1);

                    if(name == "SMALL-STEP")        Settings.SMALL_STEP = stoll(value);
                    else if(name == "BIG-STEP")     Settings.BIG_STEP = stoll(value);
                    else if(name == "ITERATIONS")   Settings.ITERATIONS = stoll(value);
                    else if(name == "SEED")         Settings.SEED = stoll(value);
                    else if(name == "SHRINK")       Settings.SHRINK = stoll(value);
                    else if(name == "METRIC")       Settings.METRIC = Config::USERS_TRADING; // There is no other METRIC
                    else validOption = false;
                } else validOption = false;

                if(!validOption) {
                    cout << "Unknown option \"" << option << "\".\n";
                    assert(false);
                }

                Metadata.inputChecksum = md5(Metadata.inputChecksum + option);
                Metadata.options.push_back(option);
            }
        }
        else if(line == "!BEGIN-OFFICIAL-NAMES"){
            while (getline(cin, line) && line != "!END-OFFICIAL-NAMES") {
                Metadata.inputChecksum = md5(Metadata.inputChecksum + line);
                istringstream iss(line);
                string tag;
                iss >> tag; if(!Settings.CASE_SENSITIVE) utils::up(tag);

                assert(Items.count(tag) == 0); // Repeated tag in official names

                int elems = Items.size();
                Items[tag] = Specimen{.index = elems, .tag = tag, .dummy = false};
                Tags[elems] = tag;
                Metadata.totalRealItems++;
            }
        } else {
            // Wishlists
            Metadata.inputChecksum = md5(Metadata.inputChecksum + line);
            assert(line[0] == '('); // Garbage line
            
            if(Settings.REQUIRE_OFFICIAL_NAMES) assert(Items.size() > 0); // Cannot wishlist without listing official names
            istringstream iss(line);
            string temp, username;

            getline(iss, temp, '(');
            getline(iss, username, ')');
            assert(temp.size() == 0 && username.size() > 0);
            
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

            if(!Settings.CASE_SENSITIVE){ utils::up(tag); utils::up(username); }
            if(tag[0] == '%') tag += username;
            if(!Items.count(tag)){
                if(Settings.REQUIRE_OFFICIAL_NAMES) assert(tag[0] == '%'); // Must be a dummy not seen before
                int elems = Items.size();
                Items[tag] = Specimen{.index = elems, .tag = tag, .username = username, .dummy = tag[0] == '%'};
                Tags[elems] = tag;
                Metadata.totalRealItems += tag[0] != '%';
            }
            
            if(Items[tag].username.empty()){
                Items[tag].username = username;
            }
            else assert (Items[tag].username == username);

            while(iss >> temp){
                if(!Settings.CASE_SENSITIVE) utils::up(temp);
                if(temp[0] == '%') temp += username;
                if(!Items.count(temp)){
                    if(Settings.REQUIRE_OFFICIAL_NAMES) assert(temp[0] == '%'); // Must be a dummy not seen before
                    int elems = Items.size();
                    Items[temp] = Specimen{.index = elems, .tag = temp, .dummy = temp[0] == '%'};
                    Tags[elems] = temp;
                    Metadata.totalRealItems += temp[0] != '%';
                }

                Items[temp].wishlist.push_back(Items[tag].index);
            }
        }
    }

    solve_gurobi();
   
    // Prepare metadata
    for(const auto &v : bestGroups){
        Metadata.tradedItems += v.size();
        Metadata.sumSquares += v.size() * v.size();
        for(const auto &e : v){
            const string& tag = Tags[e];
            Metadata.formattingWidth = max(Metadata.formattingWidth, utils::utf8_length(Items[tag].show()) + 1);
        }
    }
    
    // Output result
    formatOutput(cout);

    return 0;
}
