# FastTradeMaximizer
A running-time optimized custom version of @chrisokasaki TradeMaximizer.

- https://github.com/chrisokasaki/TradeMaximizer
- https://boardgamegeek.com/wiki/page/TradeMaximizer

Chris' original version uses augmenting paths with a modified Dijkstra algorithm to find a perfect matching. This algorithm is said to have a complexity of $O(V * E * log U)$, though performance-wise it runs on par with Edmonds-Karp; Edmonds-Karp does have a closed form running time that's easier to compare to: $O(V * E^2)$.

This version formulates the min-cost max-flow matching problem as a linear programming problem instead and uses a specialized Simplex algorithm to solve it.  This algorithm's theoretical worst case is exponential although in practice it converges to the solution **very** fast with an average complexity of $O(V * E)$ and a good constant.

The code also comes with implementation details to make it go faster, besides the usage of modern C++ tools itself. For example, item nodes are mapped to $[0,totalItems)$ indices so we can use a Simplex implementation that uses contiguous vector memory spaces. It also implements a better heuristic than "randomly try to find better solutions", which not only finds strictly better solutions, it also does so quite faster.

# Usage

FastTradeMaximizer attempts to copy the original's behavior as much as possible, so most of its functionality is the same. That said, it uses a better way to maximize the number of trading users, so the concept of random iterations doesn't exist anymore: it iterates as long as it wants to improve the solution in each step and stops when it can no longer do anything to improve it.

## Compile

    g++ main.cpp -o ftm.exe

<sup><sup>You need to compile C++17, if your compiler is not up to date ```-std=c++17``` might be helpful.<sup><sup>

- For some reason installing an updated version of ```g++``` on Windows proved itself not to be as simple as doing one or two google searches. You can follow Microsoft's straightforward installation guide here: https://code.visualstudio.com/docs/cpp/config-mingw#_installing-the-mingww64-toolchain

## Run

    ftm.exe < wants.txt

# Benchmarks

All testcase files can be found in the ```/testcases``` directory. This is not an objective benchmark because I'm using some official results that were probably ran in a sligtly slower computer than mine, it's mostly a showcase of improved solutions + a lot faster. I used the following command lines to run both programs:

    java -jar tm-threaded.jar < input.txt > output.txt
    ftm.exe < input.txt > output.txt

| Testcase    | TradeMaximizer Version 1.5c multi-threaded                               | FastTradeMaximizer Version 0.3                     |
|-------------|--------------------------------------------------------------------------|----------------------------------------------------|
| ARG2024May  | 421 users in 1 iteration after 259650ms (4min 19sec 650ms)               | 427 users after 35941ms (35sec 941ms)              |
| US2024Jan   | 300 users in 72 iterations after 4341261ms (1hrs 12min 21sec 261ms)      | 313 users in 16999ms (16sec 999ms)                 |
| US2024Feb   | 222 users in 72 iterations after 1211105ms (20min 11sec 105ms)           | 230 users in 15609ms (15sec 609ms)                 |
| US2024Apr   | 241 users in 72 iterations after 2633036ms (43min 53sec 36ms)            | 250 users in 42214ms (42sec 214ms)                  |
| US2024May   | 251 users in 72 iterations after 3685621ms (1hrs 1min 25sec 621ms)       | 257 users in 33832ms (33sec 832ms)                 |

# Additional comments

- Item nodes have a $\texttt{supply}\in\mathbb{N}$ of flow, and edges have $\texttt{lower}$ and $\texttt{upper}$ bound flow requirements. By default, senders have $\texttt{supply=1}$, receivers $\texttt{supply=-1}$, and edges $\texttt{lower=0}, \texttt{upper=1}$. This may let you think of a new feature, for example $\texttt{lower=upper=1}$ **forces** an edge to be selected. You could also set $\texttt{supply=0}$ to represent a "passing by" optional node, or $\texttt{supply=2}$ to let a node get matched to up to 2 edges.

- Edges also have a $\texttt{cost}\in\mathbb{N}$ per unit of flow associated ($\texttt{cost=1}$ by default and $\texttt{cost=INF}$ for edges we don't want to use, like self loops). Conventional flow algorithms will forbid negative values or may require some sort of special initialization, a linear solver doesn't care.

- Priorities seem a bit useless so they were not implemented, they can be included by tweaking edges' $\texttt{cost}$. Just remember that increasing an edge's cost makes it less prioritary than **all** other edges, not just yours.

- Random generation and the program itself are completely different from the Java version, so it's impossible to reproduce the same outputs from previous Math Trades given the same $\texttt{SEED}$. However the number of maximum trades should be equal and the tracked metric should be equal or better.

- Simplex solves a superset of the problem, we don't have any generic graph, it's bipartite.

- If you're on Linux and your CPU supports it, you might want to experiment building with #pragma flags: ```#pragma GCC target("avx2,bmi,bmi2,popcnt,lzcnt")```
