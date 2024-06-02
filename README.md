# FastTradeMaximizer
A running-time optimized custom version of @chrisokasaki TradeMaximizer.

- https://github.com/chrisokasaki/TradeMaximizer
- https://boardgamegeek.com/wiki/page/TradeMaximizer

Chris' original version uses augmenting paths with a modified Dijkstra algorithm to find a perfect matching. This algorithm is said to have a complexity of $O(V * E * log U)$, though performance-wise it runs on par with Edmonds-Karp; Edmonds-Karp does have a closed form running time that's easier to compare to: $O(V * E^2)$.

This version formulates the min-cost max-flow matching problem as a linear programming problem instead and uses a specialized Simplex algorithm to solve it.  This algorithm's theoretical worst case is exponential although in practice it converges to the solution **very** fast with an average complexity of $O(V * E)$ and a good constant.

The code also comes with implementation details to make it go faster, besides the usage of modern C++ tools itself. For example, item nodes are mapped to $[0,totalItems)$ indices so we can use a Simplex implementation that uses contiguous vector memory spaces.

# Usage

FastTradeMaximizer attempts to copy the original's behavior for as long as possible, mostly for testability reasons. So most of its functionality is the same.

## Compile

    g++ main.cpp

<sup><sup>You need to compile C++17, if your compiler is not up to date ```-std=c++17``` might be helpful.<sup><sup>

- For some strange reason installing an updated version of ```g++``` on Windows proved itself not to be as simple as doing one or two google searches. You can follow Microsoft's straightforward installation guide here: https://code.visualstudio.com/docs/cpp/config-mingw#_installing-the-mingww64-toolchain

## Run

    a.exe < wants.txt

# Benchmarks

I used the following command lines to run both programs:

    java -jar tm-threaded.jar < input.txt > output.txt
    a.exe < input.txt > output.txt

| Testcase   | Size       | Iterations | TradeMaximizer Version 1.5c multi-threaded | FastTradeMaximizer Version 0.1 |
|------------|------------|------------|--------------------------------------------|--------------------------------|
| ARG2024May | 6265 items | 1          | 259650ms (4min 19sec 650ms)                | 4116ms (4sec 116ms)            |
| US2024Jan  | 7808 items | 1          | 50971ms (50sec 971ms)                      | 1789ms (1sec 789ms)            |
| US2024Jan  | 7808 items | 12         | 457804ms (7min 37sec 804ms)                | 2807ms (2sec 807ms)            |
| BR2024May  | 958 items  | 96         | 1981ms (1sec 981ms)                        | 336ms                          |
| US2024May  | 6291 items | 24         | 711742ms (11min 51sec 742ms)               | 5073ms (5sec 73ms)             |
| US2024May  | 6291 items | 1          | 45440ms (45sec 440ms)                      | 1852ms (1sec 852ms)            |

# Additional comments

- Item nodes have a $supply\in\mathbb{N}$ of flow, and edges have a $lower\_bound$ and an $upper\_bound$ flow requirements. By default, senders have $supply=1$, receivers $supply=-1$, and edges $lower\_bound = 0, upper\_bound = 1  \text{ or } INF$. This may let you think of a new feature, for example $lower\_bound = upper\_bound = 1$ **forces** an edge to be selected. You could also set $supply=0$ to represent a "passing by" optional node, or $supply=2$ to let a node get matched to up to 2 edges.

- Priorities seem a bit useless so they were not implemented, they can be easily included by tweaking edges' $\text{cost}$. Just remember that increasing an edge's cost makes it less prioritary than **all** other edges, not just yours.

- Random generation and the program itself are completely different from the Java version, so it's impossible to reproduce the same outputs from previous Math Trades given the same $\text{SEED}$. However the number of maximum trades should be equal and the tracked metric should be the equal or better.

- Simplex solves a superset of the problem, we don't have any generic graph, it's bipartite.

- If you're on Linux and your processor supports it, you might want to experiment building with #pragma flags: ```#pragma GCC target("avx2,bmi,bmi2,popcnt,lzcnt")```
