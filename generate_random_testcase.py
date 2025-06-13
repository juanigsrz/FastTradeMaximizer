import random
from collections import Counter

# --- Configuration ---
# You can tweak these values to generate different kinds of test cases.

NUM_BOARDGAMES = 100        # Total number of unique board games in the ecosystem.
NUM_USERS = 25              # Number of traders participating.
GAMES_PER_USER_MIN = 5      # Minimum number of games a user starts with.
GAMES_PER_USER_MAX = 10     # Maximum number of games a user starts with.
WISHES_PER_USER_MIN = 4     # Minimum number of wishlists a user will generate.
WISHES_PER_USER_MAX = 8     # Maximum number of wishlists a user will generate.

# --- Hub Game Configuration ---
NUM_HUB_GAMES = 3           # Number of special "hub" games that force multi-trades.
PROB_HUB_TRADE_ATTEMPT = 0.3 # Probability of attempting to create a hub-based trade per wish.

# --- Regular Trade Type Probabilities ---
# These apply to non-hub trades and should sum to 1.0
PROB_1_FOR_1 = 0.90         # e.g., {GameA} -> {GameB}
PROB_1_FOR_MULTI = 0.05     # e.g., {GameA} -> {GameB, GameC}
PROB_MULTI_FOR_1 = 0.05     # e.g., {GameA, GameB} -> {GameC}

def generate_test_cases():
    """
    Main function to generate and print the board game trade wishlists,
    featuring "hub" games to guarantee multi-item trades.
    """
    # 1. Create a master list of board games and designate Hub Games
    board_games = [f"Game_{i+1}" for i in range(NUM_BOARDGAMES)]
    random.shuffle(board_games) # Shuffle to pick random hubs
    hub_games = set(board_games[:NUM_HUB_GAMES])
    regular_games = set(board_games[NUM_HUB_GAMES:])
    
    # 2. Assign games to each user
    users = {}
    all_owned_games = []
    user_ids = [f"User_{i+1}" for i in range(NUM_USERS)]

    # Distribute regular games
    for user_id in user_ids:
        num_games = random.randint(GAMES_PER_USER_MIN, GAMES_PER_USER_MAX)
        # Ensure we don't try to sample more games than exist
        if num_games > len(regular_games):
            num_games = len(regular_games)
        
        owned_games = set(random.sample(list(regular_games), num_games))
        users[user_id] = owned_games
        all_owned_games.extend(list(owned_games))

    # Distribute hub games to distinct users to ensure they are in play
    users_with_hubs = random.sample(user_ids, len(hub_games))
    for i, hub_game in enumerate(hub_games):
        user_id = users_with_hubs[i]
        users[user_id].add(hub_game)
        all_owned_games.append(hub_game)

    # 3. Determine popularity for REGULAR games only
    game_popularity = Counter([g for g in all_owned_games if g in regular_games])
    sorted_by_popularity = sorted(game_popularity.keys(), key=lambda g: game_popularity.get(g, 0), reverse=True)
    
    # 4. Generate wishlists for each user
    wishlists = []
    for user_id, haves in users.items():
        potential_wants = set(all_owned_games) - haves
        if not potential_wants:
            continue

        num_wishes = random.randint(WISHES_PER_USER_MIN, WISHES_PER_USER_MAX)

        for _ in range(num_wishes):
            hub_trade_made = False
            # --- Attempt to create a HUB trade ---
            if random.random() < PROB_HUB_TRADE_ATTEMPT:
                user_hub_haves = haves.intersection(hub_games)
                hub_wants = potential_wants.intersection(hub_games)

                # Priority 1: User has a hub game and wants to trade it away
                if user_hub_haves:
                    hub_to_offer = random.choice(list(user_hub_haves))
                    # Wants two desirable regular games in return
                    potential_regular_wants = [g for g in sorted_by_popularity if g in potential_wants]
                    if len(potential_regular_wants) >= 2:
                        wants = random.sample(potential_regular_wants[:len(potential_regular_wants)//2], 2)
                        wishlists.append(f"{hub_to_offer} -> {' '.join(wants)}")
                        hub_trade_made = True

                # Priority 2: User wants a hub game
                elif hub_wants and not hub_trade_made:
                    hub_to_want = random.choice(list(hub_wants))
                    # Offers two of their common regular games
                    user_regular_haves = [g for g in reversed(sorted_by_popularity) if g in haves]
                    if len(user_regular_haves) >= 2:
                        offers = random.sample(user_regular_haves[:len(user_regular_haves)//2], 2)
                        wishlists.append(f"{' '.join(offers)} -> {hub_to_want}")
                        hub_trade_made = True

            # --- Generate a REGULAR trade if no hub trade was made ---
            if not hub_trade_made:
                trade_type = random.random()
                
                # Filter to only regular games for these trades
                haves_regular = haves.intersection(regular_games)
                potential_wants_regular = potential_wants.intersection(regular_games)

                if not haves_regular or not potential_wants_regular:
                    continue

                # --- 1-for-Multi ---
                if trade_type < PROB_1_FOR_MULTI:
                    if len(potential_wants_regular) < 2: continue
                    offer = random.sample(list(haves_regular), 1)
                    wants = random.sample(list(potential_wants_regular), 2)
                    wishlists.append(f"{offer[0]} -> {' '.join(wants)}")

                # --- Multi-for-1 ---
                elif trade_type < PROB_1_FOR_MULTI + PROB_MULTI_FOR_1:
                    if len(haves_regular) < 2: continue
                    offers = random.sample(list(haves_regular), 2)
                    want = random.sample(list(potential_wants_regular), 1)
                    wishlists.append(f"{' '.join(offers)} -> {want[0]}")

                # --- 1-for-1 ---
                else:
                    offer = random.sample(list(haves_regular), 1)
                    want = random.sample(list(potential_wants_regular), 1)
                    wishlists.append(f"{offer[0]} -> {want[0]}")

    return wishlists

if __name__ == "__main__":
    # Generate the wishlists
    generated_trades = generate_test_cases()
    
    # Print the results to the console
    print(f"--- Generated {len(generated_trades)} Trade Wishlists ---")
    for trade in generated_trades:
        print(trade)

    # Optional: Save to a file
    with open("trade_testcase.txt", "w") as f:
        for trade in generated_trades:
            f.write(trade + "\n")
    print("\n--- Test case saved to trade_testcase.txt ---")
