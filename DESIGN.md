# Space Trader — Design Document

> A console trading & survival game in the tradition of bsdgames.
> Written in C99, ncurses, minimal external dependencies.

---

## 1. Concept & Premise

You are a down-on-your-luck interstellar trader, stranded on the frontier colony world of
**Kepler's Reach**.  Your ship — the only thing you own worth anything — sits in impound at
the **Starport**, locked behind a **10 000 credit** docking debt you have no way to pay.

You step out of the port authority office with **100 credits** in your pocket and ten
hitpoints between you and an unmarked grave.

**Win condition:** Accumulate 10 000 credits, return to the Starport, and pay off the impound
fee.  Your ship lifts off and you leave this rock behind forever.

**Lose condition:** Your hit points reach zero.  You die.  Game over.

There is no time limit and no other ending — only wealth or death.

---

## 2. High-Level Architecture

```
spacetrader/
├── main.c            — entry point, game loop, signal handling
├── ui.c / ui.h       — all ncurses rendering, window layout, input handling
├── world.c / world.h — location graph, travel, location descriptions
├── market.c / market.h — commodity prices, buy/sell, supply/demand
├── player.c / player.h — player stats, inventory, equipment, health
├── combat.c / combat.h — turn-based on-foot combat engine
├── encounter.c / encounter.h — random encounter table & resolution
├── prospect.c / prospect.h — prospecting / foraging mechanic
├── gamble.c / gamble.h — blackjack and roulette mini-games
├── event.c / event.h — scripted one-time story events
├── save.c / save.h   — save/load game state (single flat binary file)
├── util.c / util.h   — RNG, string helpers, math utils
└── Makefile
```

### Game Loop (state machine)

```
TITLE → NEW_GAME
           │
           ▼
      ┌─ LOCATION ─────────────────────────────────┐
      │   (Starport, settlement, or wilderness)     │
      │                                             │
      │  Starport:  pay impound → WIN               │
      │  Settlement: STORE · BAR · CLINIC           │
      │  Wilderness: ENCOUNTER · PROSPECT           │
      └─────────────────────────────────────────────┘
           │
      [Travel chosen]
           │
      TRAVEL (1 turn, possible road encounter)
           │
           ▼
      LOCATION  (loop)
```

All states are encoded in a `game_state_t` enum.  Each state registers
`enter_`, `handle_input_`, and `render_` function pointers.

### Memory Model

- All game state lives in a single `game_t` struct, stack-allocated in `main`.
- No heap allocation except ncurses windows and the news-ticker ring buffer.
- Save file is a straight `fwrite` of `game_t` with a version-stamped header and CRC32.
- v0.1 uses explicit fixed caps so the save blob stays bounded and heap-free:
  `MAX_DROP_STACKS_PER_LOCATION = 8`, `MAX_BAR_MISSIONS = 2` per Bar,
  `MAX_ACTIVE_MISSIONS = 4`, and `NEWS_TICKER_LINES = 8`.
- NPCs do not carry independent mutable state in v0.1; their behaviour is derived from
  global reputation plus location-specific scripted dialogue.

---

## 3. World Design

### 3.1 The Map

The game world is a single planet.  There are **9 named locations** connected by a simple
road/trail network:

```
        [Dustwallow]      [Ashfield]      [Ironpass]
        (wilderness)     (settlement)     (wilderness)
              \               |               /
               \              |N             /
                \             |             /
    [Coldwater]--W-----[STARPORT]-----E--[Brokenhill]
    (settlement)        (hub/start)       (settlement)
                /             |             \
               /              |S             \
              /               |               \
        [The Barrens]    [Millhaven]      [Saltmarsh]
        (wilderness)     (settlement)     (wilderness)
```

Connections:
- **Starport** ↔ Ashfield (N), Brokenhill (E), Millhaven (S), Coldwater (W)
- **Ashfield** ↔ Starport, Dustwallow (NW), Ironpass (NE)
- **Brokenhill** ↔ Starport, Ironpass (NW), Saltmarsh (SW)
- **Millhaven** ↔ Starport, Saltmarsh (SE), The Barrens (SW)
- **Coldwater** ↔ Starport, The Barrens (SE), Dustwallow (NE)
- **Dustwallow** ↔ Ashfield, Coldwater
- **Ironpass** ↔ Ashfield, Brokenhill
- **Saltmarsh** ↔ Brokenhill, Millhaven
- **The Barrens** ↔ Millhaven, Coldwater

Getting from one settlement to another requires passing through either Starport (1 hop each
way) or the shared wilderness area between them (faster but dangerous).  Wilderness areas
have no direct connection to Starport; reaching the hub always requires passing through a
settlement first.

### 3.2 Location Types

| Location     | Type        | Position | Connections                    | Notes                                    |
|--------------|-------------|----------|--------------------------------|------------------------------------------|
| Starport     | Hub         | Centre   | N/E/S/W (all four settlements) | Start point; pay impound here to win     |
| Ashfield     | Settlement  | N        | Starport, Dustwallow, Ironpass | Agricultural — cheap food, expensive ore |
| Brokenhill   | Settlement  | E        | Starport, Ironpass, Saltmarsh  | Mining — cheap ore/metal, pricey food    |
| Millhaven    | Settlement  | S        | Starport, Saltmarsh, Barrens   | Farming commune — cheapest food, no ore  |
| Coldwater    | Settlement  | W        | Starport, The Barrens, Dustwallow | Trade post — balanced prices          |
| Dustwallow   | Wilderness  | NW       | Ashfield, Coldwater            | Swampy wetlands; wildlife & rare herbs   |
| Ironpass     | Wilderness  | NE       | Ashfield, Brokenhill           | Mountain pass; minerals, bandit activity |
| Saltmarsh    | Wilderness  | SE       | Brokenhill, Millhaven          | Coastal marsh; salvage, storms           |
| The Barrens  | Wilderness  | SW       | Millhaven, Coldwater           | Arid flats; rare gems, rockslides        |

### 3.3 Starport

The Starport acts as the hub and is the most developed location.  It has:
- A **Market** (broadest commodity selection, moderate prices; also sells the Cargo Hover)
- A **Bar** (gambling, rumours)
- A **Hospital** (health restoration)
- The **Port Authority** desk (pay impound fee to win)
- A **notice board** showing current commodity prices at all known locations
  (updated each time the player visits a settlement)

### 3.4 Settlements

Each of the four settlements has exactly three services:

| Service | Function                                                       |
|---------|----------------------------------------------------------------|
| **Store**   | Buy and sell commodities; buy arms and armor              |
| **Bar**     | Rumours, optional brawls, blackjack and roulette gambling |
| **Clinic**  | Restore up to 3 HP for 25 Cr per point                   |

### 3.5 Wilderness Areas

Wilderness areas have no permanent services.  Each visit the player may:
- **Prospect** — spend a turn searching for goods (see §6).
- **Rest** — sleep rough for the night: recover **1 HP**, costs nothing.  Still rolls for
  a wilderness encounter (sleeping in the open is never fully safe).
- **Pass through** — continue to an adjacent location without stopping.

Every turn spent in a wilderness area rolls for a **random encounter** (see §7).

### 3.6 Location Descriptions

Each location displays a short description and optional ASCII art when the player arrives.
These are shown in the main viewport and remain visible until the player takes an action.

---

**STARPORT**
```
        __|__
   --+--(___)--+--
     |  BERTH  |
  .--+----+----+--.
  |  PORT AUTHORITY |
  |_________________|
  | MARKET | HOSP.  |
  '---------'-------'
```
> The shadow of your impounded ship falls across the landing apron every morning —
> a daily reminder of what you owe and what you stand to lose.  The Starport is the
> beating heart of Kepler's Reach: loud, transactional, and indifferent.  Fuel-smell
> and fry-oil and the distant clang of the maintenance bays.  The port authority
> office is just ahead.  Ten thousand credits, and you walk out of here forever.

---

**ASHFIELD** *(Settlement — North)*
```
  _____    _____    _____
 | . . |  | . . |  |     |
 | . . |  | . . |  | HAY |
 |FIELD|  |FIELD|  | BARN|
 '-----'  '-----'  '-----'
    ___________________
   | ASHFIELD CROSSING |
   '-------------------'
```
> Rolling fields of pale grain stretch to the horizon under a flat grey sky.  A
> cluster of low buildings — trading post, bar, whitewashed clinic — huddles at the
> crossroads like settlers unsure whether to stay.  Farmhands move through the rows
> with the unhurried rhythm of people who have nowhere better to be.  The air is
> clean out here, if a little dusty.

---

**BROKENHILL** *(Settlement — East)*
```
    /\      /\      /\
   /  \    /##\    /  \
  / ## \  /####\  / ## \
 '------''------''------'
  ======= SHAFT 3 =======
  | BROKENHILL WORKS Co. |
  '======================'
```
> The hills here are honeycombed with shafts, and the settlement beneath them is
> stained deep ochre from decades of ore dust.  Conveyor belts rattle overhead
> between squat warehouses.  The workers look hard-used and say little to
> outsiders.  Metal and ore move cheaper here than anywhere on the Reach, and the
> bar does a fierce trade come evening.

---

**COLDWATER** *(Settlement — West)*
```
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  ~  ~  RIVER CROSSING  ~  ~  ~  ~
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    =====|  FERRY  |=====
         |_________|
    .----+----------+----.
    |  COLDWATER STATION  |
    '--------------------+'
```
> Built at a broad river ford, Coldwater has always been a crossroads.  Goods
> move through here from every direction and the locals have developed a
> reputation for fair dealing and inflated self-importance in roughly equal
> measure.  The store is the best-stocked of any settlement, prices tend to
> the middle of everything — not cheap, but reliable.

---

**MILLHAVEN** *(Settlement — South)*
```
   [SILO]   [SILO]   [SILO]
     ||        ||       ||
  +--++-+----+-++-+--+--++-+
  |     MILLHAVEN COMMONS   |
  |   COOPERATIVE FARMING   |
  +-------------------------+
```
> A collective farming settlement in the flat southern lowlands.  Everything
> is shared here, or so the locals insist.  They grow more food than they can
> possibly eat and have little interest in much else.  The atmosphere is quiet
> and faintly suspicious of strangers, but no one turns away a paying customer.
> The grain prices alone are worth the trip south.

---

**DUSTWALLOW** *(Wilderness — Northwest)*
```
  ~  ~~ ~~~~  ~  ~~ ~~~~  ~
  ~~   ~~~~  ~~   ~~~~  ~~~
       DUSTWALLOW FENS
  ~~ ~~~~  ~~  ~ ~~~~  ~~ ~
  ~  ~~ ~~~~  ~  ~~ ~~~~  ~
```
> The ground here is never quite solid.  Knee-high fog clings to the reed beds
> and the twisted silhouettes of drowned trees.  Something large moves in the
> murk to the north, unconcerned by your presence.  The air smells of rot and
> green growing things in equal measure.  Valuable herbs push up through the
> shallows if you know what to look for — and can find them before something
> finds you.

---

**IRONPASS** *(Wilderness — Northeast)*
```
        /\          /\
       /  \  IRON  /  \
      / /\ \ PASS / /\ \
     /  ||  \    /  ||  \
    '---++---'--'---++---'
        ||    ||    ||
  ======++====++====++======
```
> A narrow defile between two granite ridges, the only practical route through
> the northeastern range.  The wind off the peaks is cold year-round and the
> trail is littered with rusted detritus from previous expeditions.
> Ore-bearing rock juts from every cliff face.  Bandits know this terrain
> better than you do, and they know you have to pass through.

---

**SALTMARSH** *(Wilderness — Southeast)*
```
  =-=-=-=-=-=-=-=-=-=-=-=-=-=
  |  ~~  ~~  ~~  ~~  ~~  ~~ |
  |  ~~   SALTMARSH   ~~    |
  |  ~~  ~~  ~~  ~~  ~~  ~~ |
  =-=-=-=-=-=-=-=-=-=-=-=-=-=
         OPEN COASTLINE
```
> The coast here is a tangle of tidal channels, salt-bleached mangrove-analogues,
> and the half-buried wreckage of craft that didn't make it to port.  Salvage
> washes in on every tide — sometimes useful, sometimes not worth the smell.
> Storm systems build quickly over the open water and arrive with almost no
> warning.  The ground underfoot is soft and makes no noise; neither will
> whatever is watching you from the reeds.

---

**THE BARRENS** *(Wilderness — Southwest)*
```
  .  .  .  .  .  .  .  .  .
   .   THE BARRENS    .   .
  .  .  .  .  .  .  .  .  .
   . cracked clay . rock .
  .  .  .  .  .  .  .  .  .
```
> A vast expanse of cracked clay and heat-shimmered rock, the Barrens are as
> close to nothing as Kepler's Reach gets.  The sparse vegetation is thorned
> and colourless.  The silence is thick enough to notice.  Deep below the
> parched surface, geological upheaval has pushed gemstone veins close to the
> rock face — patient prospectors do well here, if they can manage the heat
> and whatever passes for wildlife in a place like this.

---

### 3.7 Named NPCs

Each service location has a named NPC whose greeting and demeanour shift across three
reputation tiers:

- **Low** (reputation ≤ −2): wary, cold, or outright hostile
- **Neutral** (−1 to +1): professional, transactional
- **High** (≥ +2): warm, forthcoming; clinic and store NPCs offer deterministic discounts

---

#### STARPORT

**Mags** *(Bartender — The Anchor Bar)*
> A barrel-shaped woman with grey locs and a scowl that appears to be structural.
> She's been behind this bar since before the port authority building had a second
> floor, and she has formed opinions about everyone who's ever walked through the door.

- *Low:* "You look like trouble. Drink fast and don't start anything."
- *Neutral:* "What'll it be. And don't lean on the bar."
- *High:* "There you are. Usual spot's free. First one's on the house tonight."

**Dr. Ananya Patel** *(Hospital — Starport Medical)*
> Precise, unhurried, and constitutionally incapable of small talk.  She runs the
> only proper hospital on the Reach with the weary efficiency of someone who didn't
> expect to still be here a decade later.

- *Low:* "I treat everyone.  That doesn't mean I have to converse with everyone."
- *Neutral:* "Where does it hurt. How long ago. Sit down."
- *High:* "You again.  Let me take a look — and try to avoid whatever caused this."

**Juno Creed** *(Market — Starport Trading Floor)*
> Sharp eyes behind sharper glasses, and a voice like a closing ledger.  Juno has
> priced every commodity on this planet at least twice and forgotten more about
> supply chains than most people ever learn.

- *Low:* "I don't do charity and I don't do credit.  Cash, up front."
- *Neutral:* "Here to buy or to look?  Either way, don't touch the merchandise."
- *High:* "I've been saving something back for you, actually.  Come have a look."

---

#### ASHFIELD *(Settlement — North)*

**Old Henk** *(Bartender — The Fieldhand)*
> Henrik Voss is enormous, red-faced, and almost certainly the most cheerful person
> on Kepler's Reach.  He brews his own liquor from local grain, pours generous, and
> remembers every name he's ever heard.

- *Low:* "You're new. That's fine. Keep your hands where I can see 'em, that's all I ask."
- *Neutral:* "Pull up a stool, friend. What can I get you?"
- *High:* "Ha! Look who's back. Sit down, sit down — I saved you some of the good batch."

**Nurse Sera Yun** *(Clinic — Ashfield Infirmary)*
> Overqualified for a farming crossroads and aware of it, but competent enough that
> she stays anyway.  She keeps her clinic meticulously clean and her opinions largely
> to herself.

- *Low:* "I'll treat you because that's my job.  Don't make it harder than it has to be."
- *Neutral:* "Come in, sit there.  Tell me what happened."
- *High:* "Glad you came in before it got worse.  Let me sort this out — and I'll keep the fee reasonable."

**Dottie Marsh** *(Storekeeper — Marsh Provisions)*
> Fifties, wire-haired, practical in the way that only people who've run a general
> store for thirty years can be.  She has no patience for haggling and a long memory
> for regulars.

- *Low:* "I run a clean shop.  You cause trouble, you take it elsewhere."
- *Neutral:* "Looking for something in particular, or just browsing?"
- *High:* "I kept a few things back since I thought you might be coming through. Have a look."

---

#### BROKENHILL *(Settlement — East)*

**Rook** *(Bartender — The Shaft)*
> Goes by one name, has the scarred hands of someone who worked the mines before
> moving to the less dangerous occupation of serving drinks to miners.  Asks no
> questions; expects the same courtesy in return.

- *Low:* "Pay first. Drink. Leave. That's the whole arrangement."
- *Neutral:* "What do you want."
- *High:* "Good to see a straight face in here.  The usual?"

**Doc Ferris** *(Clinic — Brokenhill Medical Post)*
> Old mine doctor, now semi-retired into running the settlement clinic.  He has seen
> every industrial injury twice and stopped being surprised by any of them.  He smells
> faintly of antiseptic and strong tea.

- *Low:* "I patch up anyone who comes through that door.  Doesn't mean I have to like it."
- *Neutral:* "Sit down.  This won't take long."
- *High:* "Ah, you again.  Still getting into scrapes.  Let me take a look — I'll charge you the workers' rate."

**Gal Okonkwo** *(Storekeeper — Okonkwo Minerals & Trade)*
> Broad-shouldered, businesslike, and counts everything twice.  She built the best
> trading operation in Brokenhill from a single ore cart and a line of credit, and
> she respects anyone who takes commerce seriously.

- *Low:* "My prices aren't negotiable and neither am I.  Buy or don't."
- *Neutral:* "Stock list is on the board.  Let me know what you need."
- *High:* "Always good to deal with someone who knows the value of things.  I'll see what I can do on the price."

---

#### COLDWATER *(Settlement — West)*

**Lena Vasquez** *(Bartender — The Crossing)*
> Pours precisely, remembers every drink order from the last six months, and hears
> everything said within ten metres without appearing to listen.  The most
> well-informed person in Coldwater, possibly on the planet.

- *Low:* "A quiet bar is a good bar.  You seem like you might complicate that."
- *Neutral:* "What can I get you?"
- *High:* "I heard something you might find useful.  Drink first — then we'll talk."

**Sister Brynn** *(Clinic — Coldwater Care House)*
> A healer of the old communal tradition, gentle and non-preachy about it.  She runs
> the care house with a practical compassion and a herb garden that does most of the
> heavy lifting.

- *Low:* "Healing is given freely.  Whether I do it warmly depends on you."
- *Neutral:* "Come in, sit down.  Let me see what we're working with."
- *High:* "Back again — I'm glad you know where to come.  I'll put the kettle on."

**Tomasz Wick** *(Storekeeper — Wick & Sons Trading Post)*
> Third-generation trader, which means he was haggling before he could walk.  He
> believes everything has a price and finding the right one is the only honest work
> there is.  He respects a good counter-offer.

- *Low:* "Coldwater's a trading town.  You want something, you pay for it.  Simple."
- *Neutral:* "Good day.  Stock's fresh in.  The board shows what we have."
- *High:* "Ah, my favourite customer.  Let me show you what came in on the last run."

---

#### MILLHAVEN *(Settlement — South)*

**Cam** *(Bartender — The Commons Hall)*
> Collective member, designated hall-keeper this season.  Calm, watchful, and
> slower to warm to outsiders than most in Millhaven — which is saying something.
> Pours fair measures of the collective's own brew.

- *Low:* "We serve everyone here.  I didn't say we trusted everyone."
- *Neutral:* "Evening.  What do you need?"
- *High:* "Good to have you in.  The harvest batch just came through — it's better than last year."

**Elder Maris** *(Clinic — Millhaven Healing Collective)*
> The collective's senior healer, seventies, with the unhurried certainty of someone
> who has delivered half the current population of Millhaven.  She prefers herbs to
> pharmaceuticals and results to explanations.

- *Low:* "I heal who comes to me.  I don't ask questions.  I do ask that you return the favour."
- *Neutral:* "What happened, and when.  Give me the facts."
- *High:* "Sit, sit.  You look like you've earned a proper rest.  I'll see to this."

**Pell** *(Storekeeper — Millhaven Collective Store)*
> The collective's designated trader this rotation, earnest and slightly awkward with
> the commerce side of things.  What the collective produces, Pell sells — no more,
> no less, at the agreed collective price.

- *Low:* "We trade with everyone.  The collective voted on it.  Doesn't mean I have to enjoy it."
- *Neutral:* "Welcome to the collective store.  We have what we have."
- *High:* "Oh good, it's you.  I kept some of the good grain aside — I thought you might be passing through."

---

## 4. The Player Character

### 4.1 Starting Stats

| Attribute       | Start | Notes                                      |
|-----------------|-------|--------------------------------------------|
| Hit Points      | 10    | Maximum 10 in v0.1; no permanent HP upgrades |
| Credits         | 100   | Cash on hand                               |
| Cargo capacity  | 10    | Units the player can carry (see §4.3)      |
| Weapon          | Fists | Unarmed: 1 HP damage                        |
| Armor           | None  | 0 damage reduction                         |
| Reputation      | 0     | –5 (outlaw) … +5 (respected)               |

### 4.2 Hit Points & Death

HP is lost in combat and some wilderness events.  HP may be restored several ways:

| Method                              | HP Restored  | Cost              | Risk              |
|-------------------------------------|--------------|-------------------|-------------------|
| Hospital (Starport only)            | Up to 8 HP   | 50 Cr per point   | None              |
| Clinic (any settlement)             | Up to 3 HP   | 25 Cr per point   | None              |
| Rent a room at the Bar              | +2           | 20 Cr             | None              |
| Sleep rough (wilderness)            | +1           | Free              | Encounter still rolls |
| Bandage (item, in or out of combat) | +1           | 15 Cr to buy (single use) | None         |
| Medkit (item, in or out of combat)  | +2 per use   | 60 Cr to buy (3 uses)     | None         |

At **HP = 0** the player dies.  A short epitaph is shown; the score is recorded in the
high-score table; the game ends.  There is no resurrection.

### 4.3 Cargo

The player carries goods in a personal pack with **10 cargo units** of capacity.
Each commodity occupies a defined number of units (see §5).  Cargo is tracked by
**explicit container** rather than as a single pooled number: every stack is assigned
to exactly one owned container.

- **Pack:** 10 units by default; always stays with the player.
- **Sturdy Pack:** store-bought upgrade that increases the pack from 10 to 20 units.
- **Mule panniers:** +10 units when the player owns a mule.
- **Cart bed:** +20 additional units when a cart is attached to a living mule, for
  **+30 total** cargo from mule + cart.
- **Cargo Hover:** +100 units; always follows the player.

The inventory screen shows which container each stack occupies, and the player may move
stacks between owned containers whenever enough free space exists.  Manually dropping
cargo places the selected stack into the current location's dropped-item list (see
§8.8) rather than destroying it.  If a mule dies, only cargo assigned to the mule
panniers or attached cart is dropped; pack and Cargo Hover contents remain with the
player.

### 4.4 Reputation

Reputation is modified by player actions:

| Action                            | Change  |
|-----------------------------------|---------|
| Complete a Store delivery mission | +1      |
| Win a fair fight in the Bar       | +1      |
| Rob / attack a neutral NPC        | −2      |
| Caught cheating at gambling       | −1      |
| Caught carrying contraband        | −1      |
| Donate to clinic (optional)       | +1      |

Reputation affects:
- Bar NPC dialogue and brawl frequency.
- Whether bandits demand a bribe or attack on sight.
- Settlement clinic prices: **25 Cr/HP** normally, **20 Cr/HP** at reputation **≥ +2**.
- Store and Market buy prices for **legal** goods, weapons, armor, and equipment are
  reduced by **5%** at reputation **≥ +2** (rounded down to whole credits).  Sell prices
  are unchanged.  High-reputation "reserved stock" dialogue is flavour only in v0.1.
- Hospital prices are fixed (no reputation discount).

---

## 5. Commodities

### 5.1 Commodity Table

Fifteen tradeable commodities in four categories.  All prices in credits (Cr).
**Bulk goods** (marked †) require a mule+cart to transport in any meaningful quantity.

| #  | Name                | Category     | Base Price | Cargo Units | Legal? | Notes                                    |
|----|---------------------|--------------|-----------|-------------|--------|------------------------------------------|
|  0 | Food Rations        | Consumable   |   10 Cr   |  1.0        | Yes    | Always in demand                         |
|  1 | Grain & Seed        | Consumable   |    6 Cr   |  1.0        | Yes    | Agricultural staple                      |
|  2 | Livestock †         | Consumable   |  250 Cr   | 20.0        | Yes    | Per head; needs cart; big profit if moved|
|  3 | Liquor              | Consumable   |   35 Cr   |  0.5        | Yes    | High price variance                      |
|  4 | Medicinal Herbs     | Medicine     |   50 Cr   |  0.5        | Yes    | Rare; big profit at clinics              |
|  5 | Narcotics           | Medicine     |  200 Cr   |  0.5        | No     | Illegal; black market only               |
|  6 | Raw Ore †           | Industrial   |  150 Cr   | 20.0        | Yes    | Per load; cheap at Brokenhill            |
|  7 | Refined Metal †     | Industrial   |  400 Cr   | 15.0        | Yes    | Per ingot bundle; good margin            |
|  8 | Construction Matl. †| Industrial   |  200 Cr   | 20.0        | Yes    | Per load; always needed, low margin      |
|  9 | Tools & Hardware    | Industrial   |   55 Cr   |  1.0        | Yes    | Steady demand at farming towns           |
| 10 | Furs & Hides        | Luxury       |   60 Cr   |  1.0        | Yes    | Found in wilderness (Dustwallow)         |
| 11 | Spices              | Luxury       |  120 Cr   |  0.5        | Yes    | Rare prospect; good margin               |
| 12 | Gemstones           | Luxury       |  300 Cr   |  0.1        | Yes    | Rare prospect; very high value           |
| 13 | Artifacts           | Luxury       |  500 Cr   |  0.2        | Yes*   | Restricted; settlement stores refuse them|
| 14 | Stolen Goods        | Illegal      |   30 Cr   |  1.0        | No     | Fenced at Bar; illegal everywhere        |

`*` Artifacts are not illegal.  Settlement stores never buy them; the **Starport Market**
buys them openly at **90% of current market price**, and any Bar fence will buy them at
**70% of base price**.  
`†` Bulk good — one unit fills 15–20 cargo slots.  Practical carrying capacities:

| Setup                          | Capacity | Livestock | Ore/Constr. | Refined Metal |
|--------------------------------|----------|-----------|-------------|---------------|
| Pack only                      |   10     | 0         | 0           | 0             |
| Pack + Mule                    |   20     | 0         | 0           | 1             |
| Pack + Mule + Cart             |   40     | 2         | 2           | 2             |
| Pack + Cargo Hover             |  110     | 5         | 5           | 7             |
| Pack + Mule + Cart + Cargo Hover| 140     | 7         | 7           | 9             |

### 5.2 Price Formula

```
price = base_price
      × location_modifier(location, commodity)   // 0.5 – 2.0
      × supply_modifier(current_stock)            // 0.6 – 1.8
      + rand_jitter(±10%)
```

Prices are fixed for the duration of a visit and refresh on the player's next arrival
(after at least one turn has passed elsewhere).

### 5.3 Location Price Tendencies

| Location   | Cheap                        | Expensive                    |
|------------|------------------------------|------------------------------|
| Starport   | Tools, Refined Metal         | Food, Furs                   |
| Ashfield   | Food, Grain, Liquor          | Raw Ore, Tools, Refined Metal|
| Brokenhill | Raw Ore, Refined Metal, Construction Materials | Food, Medicinal Herbs |
| Coldwater  | Balanced — no commodity-specific bias; legal goods stay within 0.95–1.05× base before supply | |
| Millhaven  | Food, Grain, Furs            | Refined Metal, Tools, Spices |

### 5.4 Supply & Demand

Each location's store tracks `int16_t stock[NUM_COMMODITIES]`.  Buying decreases stock;
selling increases it.  Stock drifts back toward a location baseline at ~3 units per visit.

A commodity with `stock = 0` cannot be purchased.  A flooded commodity (stock at maximum)
pays poorly.

### 5.5 Contraband

Narcotics and Stolen Goods cannot be bought or sold at legitimate Stores.  They may only
be obtained via wilderness encounters or the Bar's back-room fence.  If the player is caught
carrying contraband by a **road patrol encounter**, all contraband is confiscated and a
fine equal to the **total base value** of the seized goods is levied (**minimum 100 Cr**,
rounded up to the next 10 Cr).  If the player cannot pay in full, the patrol seizes all
cash on hand instead.

---

## 6. Prospecting

Prospecting is the act of searching a wilderness area for goods.  It takes **1 turn**
(which triggers the usual wilderness encounter roll first).

### 6.1 Prospect Outcomes

Roll 1d100 + `prospect_bonus` (see equipment):

| Roll   | Result                                             |
|--------|----------------------------------------------------|
| 01–20  | Nothing found                                      |
| 21–40  | Common goods: 1–2 units of Food or Grain, or 1 unit of Construction Materials |
| 41–60  | Useful goods: 1 unit of Raw Ore, Furs, or Medicinal Herbs                      |
| 61–75  | Uncommon goods: 1 unit of Liquor, Tools, or Refined Metal                      |
| 76–88  | Rare goods: 1 unit of Spices or Medicinal Herbs    |
| 89–96  | Valuable: 1 unit of Gemstones                      |
| 97–99  | Cache: either 2 mixed non-bulk goods or 1 bulk-good unit |
| 100+   | Artifact (unique item; very high value)            |

Results are modified by location character:
- **Dustwallow:** +10 to Furs, Medicinal Herbs; −10 to Raw Ore/Refined Metal
- **Ironpass:** +15 to Raw Ore, Refined Metal, Gemstones; −10 to Food/Furs
- **The Barrens:** +20 to Gemstones, Artifacts; +5 to Spices; nothing edible
- **Saltmarsh:** +10 to Furs, Salvage (Stolen Goods or Construction Materials); −10 to Minerals

If the player has no free cargo space, found goods are lost.

---

## 7. Random Encounters

### 7.1 Road Encounters (Travel)

Every time the player travels between locations, roll:
- Base chance: **20%** on all roads.
- Roads adjacent to wilderness areas: **35%**.

| Roll (d6) | Road Encounter             | Resolution                                    |
|-----------|----------------------------|----------------------------------------------|
| 1         | Road patrol (law officers) | Inspect cargo; confiscate contraband and levy fine if found |
| 2         | Travelling merchant        | Buy/sell 1–2 items at random prices          |
| 3         | Mugger                     | Combat or flee or surrender (lose 10–30 cr)  |
| 4         | Lost traveller             | Reputation +1 for helping; ignore = nothing  |
| 5         | Nothing                    | Uneventful                                   |
| 6         | Nothing                    | Uneventful                                   |

### 7.2 Wilderness Encounters

Every turn spent in a wilderness area (including prospecting) rolls:
- Base chance: **50%**.

| Roll (d10) | Wilderness Encounter        | Resolution                                        |
|------------|-----------------------------|----------------------------------------------------|
| 1–2        | Aggressive wildlife         | Combat (flee possible; animals do not bribe)       |
| 3–4        | Bandits                     | Combat, flee, or bribe (10–40 cr or % of cargo)   |
| 5          | Abandoned cargo             | Free goods (1–2 non-bulk units, or 1 bulk-good unit)|
| 6          | Rockslide / flash flood     | Lose 0–2 units of carried cargo; possible 1 HP dmg|
| 7          | Rival prospector            | Compete: both roll d6; winner gets +10 to prospect |
| 8          | Injured traveller           | Help (reputation +1, cost 0–20 cr) or ignore      |
| 9          | Hazard (sinkhole, bad water)| Avoid (50% chance) or take 1–2 HP damage          |
| 10         | Clear                       | No encounter this turn                            |

### 7.3 Bar Encounters

Entering the Bar always rolls for an optional event:
- **30%** — Rumour available (price tip for one commodity at a named location)
- **20%** — A local wants to brawl (player may accept, decline, or bet on the outcome)
- **10%** — Mission offered (delivery or fetch quest; see §10)
- **Remainder** — Quiet evening; just gambling available

---

## 8. Combat

Combat is **turn-based** and uses simple ASCII art.  It can occur during wilderness
encounters, road muggers, bar brawls, and certain story events.

### 8.1 Combat Loop

```
while (player.hp > 0 && enemy.hp > 0) {
    display_combat_state();
    action = player_choose_action();
    resolve_player_action(action);
    if (enemy.hp > 0) resolve_enemy_action();
    check_end_conditions();
}
```

### 8.2 Player Actions per Turn

| Action       | Effect                                                     |
|--------------|------------------------------------------------------------|
| Attack       | Deal weapon damage − target DR (minimum 0)                 |
| Defend       | +2 armor DR this turn; forfeit attack                      |
| Use item     | Apply bandage (+1 HP, single use), use medkit (+2 HP, 3 uses), etc. |
| Flee         | `P(flee) = 50% − enemy_flee_penalty` (minimum 10%, maximum 90%). Lose 1 HP if it fails. |
| Intimidate   | Morale check vs. enemy; low-morale enemies may flee        |

### 8.3 Damage Model

```
net_damage  = max(0, weapon_damage − target_armor_dr)
target.hp  -= net_damage
```

### 8.4 Arms (Weapons)

All arms are available at any settlement's Store or at the Starport Market.

| Weapon               | Damage | Cost     | Notes                              |
|----------------------|--------|----------|------------------------------------|
| Fists                | 1 HP   | —        | Default; always available          |
| Knife                | 2 HP   | 25 Cr    | Concealable                        |
| Machete              | 3 HP   | 50 Cr    | Good vs. wildlife                  |
| Slugthrower Pistol   | 4 HP   | 125 Cr   | Common sidearm                     |
| Laser Pistol         | 5 HP   | 250 Cr   | Reliable; no ammo to buy           |
| Hunting Rifle        | 6 HP   | 500 Cr   | Best non-military ranged option    |
| Blaster              | 8 HP   | 1 000 Cr | Military-grade; conspicuous        |

Only one weapon can be equipped at a time.  Others can be sold back at any Store.

### 8.5 Armor

| Armor           | Cost   | DR | Notes                                |
|-----------------|--------|----|--------------------------------------|
| None            | —      | 0  | Default                              |
| Leather Jacket  | 80 cr  | 1  | Common; no movement penalty          |
| Light Vest      | 200 cr | 2  | —                                    |
| Heavy Vest      | 450 cr | 3  | −5% flee chance                      |
| Combat Armor    | 900 cr | 5  | −10% flee chance; very conspicuous   |

### 8.6 Other Useful Equipment

| Item             | Cost   | Effect                                               |
|------------------|--------|------------------------------------------------------|
| Bandage          | 15 Cr  | Restore 1 HP; single use                             |
| Medkit           | 60 Cr  | Restore 2 HP per use; 3 uses before resupply needed  |
| Prospecting Kit  | 150 cr | +15 to all prospect rolls                            |
| Sturdy Pack      | 100 Cr  | Cargo capacity 10 → 20 units                                                                      |
| Lucky Charm      |  80 Cr  | +5 to encounter-table result rolls after an encounter triggers; does not affect encounter chance   |
| Mule             | 200 Cr  | +10 cargo in mule panniers; 8 HP; fights alongside player (bite 1 HP, kick 2 HP); permanent until killed |
| Cart             | 400 Cr  | Attaches to a mule; adds +20 cargo (+30 total with mule); mule with cart cannot kick               |
| Cargo Hover ‡    | 1500 Cr | +100 cargo capacity; does not participate in combat; sold only at Starport Market                 |

`‡` The Cargo Hover is a repulsor-lift freight platform guided by a primitive synthetic
intelligence.  It follows the player automatically at all times and cannot be dropped,
abandoned, or damaged — it is only lost permanently on the player's death.  No special
handling is required beyond the purchase.

### 8.7 Enemies

| Enemy            | HP  | DR | Damage       | Enemy flee penalty | Flees at | Found in          |
|------------------|-----|----|--------------|--------------------|----------|-------------------|
| Feral Dog        | 4   | 0  | 1            | 10%                | 25% HP   | Wilderness        |
| Wild Boar        | 8   | 1  | 3            | 20%                | Never    | Wilderness        |
| Giant Serpent    | 12  | 0  | 2 + poison   | 20%                | 10% HP   | Dustwallow        |
| Rock Cat         | 10  | 1  | 3            | 15%                | 20% HP   | Ironpass, Barrens |
| Mugger           | 6   | 0  | 2            | 0%                 | 40% HP   | Roads             |
| Bandit           | 8   | 1  | 4            | 10%                | 30% HP   | Wilderness, roads |
| Bandit Leader    | 12  | 2  | 6            | 15%                | 10% HP   | Wilderness (rare) |
| Bar Brawler      | 8   | 0  | 1            | 0%                 | 25% HP   | Bar               |

Poison (Giant Serpent): lose 1 HP/turn for 3 turns unless a Bandage or Medkit is used.

If the player owns a **Mule**, it acts as an additional combatant each round, automatically
choosing bite (1 HP) or kick (2 HP) at random.  The mule has **8 HP** and **DR 0**; if it
reaches 0 HP it is killed, any cargo assigned to its panniers is dropped at the current
location, and the player must buy a new mule.  Cargo in the pack or Cargo Hover is
unaffected (see §8.8).

A **Cart** can be purchased for 400 Cr and attached to a mule, tripling its cargo bonus
(+10 → +30 units).  A mule pulling a cart is encumbered and **cannot kick** — it may only
bite for 1 HP.  If the mule dies while pulling a cart, both the cart itself and any cargo
assigned to the cart bed are dropped at the location.  A replacement mule can be attached
to a recovered cart (no re-purchase needed).  A cart without a mule provides no cargo
benefit and cannot be carried by the player — it must stay where it is until a mule is
brought to it.

| Ally            | HP | DR | Bite | Kick          | Cargo bonus |
|-----------------|----|----|------|---------------|-------------|
| Mule            | 8  | 0  | 1 HP | 2 HP          | +10 units   |
| Mule with Cart  | 8  | 0  | 1 HP | — (disabled)  | +30 units   |

### 8.8 Dropped Cargo & World Persistence

Items and cargo can end up on the ground at a location through several means: mule death,
cart left behind after mule death, player manually dropping cargo, wilderness
abandoned-cargo encounters, or certain story events.  The Cargo Hover is explicitly
excluded from this mechanic — it always follows the player and is never left behind.

- Dropped items are stored in a per-location list and are visible to the player when they
  arrive.
- Each location can hold up to **8 dropped stacks/objects**.  Matching commodity stacks
  merge first; unique objects such as carts, weapons, and armor occupy their own slot.
  If a new drop would exceed the cap, the oldest lowest-value dropped stack at that
  location is discarded as scavenged.
- `[G]rab` picks up normal item stacks into owned containers, subject to available space in
  those specific containers.
- If the dropped object is a **Cart** and the player has a living mule with no cart
  attached, `[G]rab` reattaches it in place; otherwise the cart stays on the ground.
- Each turn that the player is **not present** at a location, every dropped item there has
  a **10% chance** of being quietly removed (assumed picked up by a passing trader).  This
  check runs once per item per turn.
- Items the player deliberately drops are treated identically to any other dropped cargo —
  there is no way to "reserve" or lock them.
- The world state (including all dropped cargo lists) is saved with the rest of the game.

---

## 9. The Bar

The Bar exists at every settlement and at the Starport.

### 9.1 Services

**Rumours:** On entering, the barkeep may share a tip (30% chance per visit):
> *"Brokenhill's short on food — I heard they're paying triple for grain this week."*

Rumours are generated from actual current price data, giving true (if possibly stale) intel.

**Rent a room:** The player may pay **20 cr** to rent a room for the night, restoring
**2 HP**.  This is safe (no encounter roll) and available at every Bar.

**Fence:** The Bar's back room will buy Stolen Goods and Artifacts at 70% of base price,
no questions asked.  This is the only legal outlet for Stolen Goods outside confiscation.

**Brawls:** Another patron may challenge the player.  The player may:
- **Accept** — fight; winner takes 10–50 cr from loser.  Losing damages HP.
- **Decline** — reputation −0 (no penalty for declining a random brawl).
- **Bet** — wager credits on yourself; double if you win; lose if you lose.

### 9.2 Blackjack

Standard blackjack rules (hit/stand/double; no split).  Dealer stands on soft 17.

- Minimum bet: 5 cr.  Maximum bet: 500 cr.
- Blackjack pays 3:2.
- House edge is realistic (~0.5%) — the game is winnable but not a reliable grind.

```
  ╔══════ BLACKJACK ══════╗
  ║  Dealer: [K♠] [??]   ║
  ║  You:   [7♥] [9♦] 16 ║
  ║  Bet: 50 cr           ║
  ║  [H]it  [S]tand [D]bl ║
  ╚═══════════════════════╝
```

### 9.3 Roulette

European roulette (single zero, 1–36 + 0).  Chip minimum: 5 cr.

Available bets:
- **Straight up** (single number): pays 35:1
- **Red/Black**: pays 1:1
- **Odd/Even**: pays 1:1
- **Low (1–18) / High (19–36)**: pays 1:1
- **Dozens (1–12, 13–24, 25–36)**: pays 2:1
- **Columns**: pays 2:1

The wheel is animated with a simple ASCII spinner.  Maximum bet per spin: 200 cr.

---

## 10. Missions

Missions are offered at the Bar (notice board on the wall) and occasionally by NPCs
encountered on the road.  There is always 0–2 available at each Bar, and the player may
hold at most **4 accepted missions** at once.

| Type             | Description                                           | Reward                         |
|------------------|-------------------------------------------------------|-------------------------------|
| Cargo Delivery   | Take N units of X from location A to location B       | 1.4× normal sell price + bonus|
| Fetch Quest      | Find and bring back a specific item (may need prospect)| 100–600 cr flat               |
| Escort           | Travel with an NPC to a destination (road encounter risk)| 50–200 cr                  |
| Elimination      | Kill a named bandit (combat mandatory)                | 200–800 cr + reputation +1   |

Missions have a soft deadline counted in player turns.  Missing it forfeits the bonus but
not the base reward (for delivery missions).  Failing an elimination mission has no penalty
beyond lost time.

Mission generation rules:
- **Cargo Delivery** missions are generated against the player's **current free cargo
  space** when the mission board is entered, so no offered job is impossible to accept
  immediately.
- Delivery missions never request contraband or artifacts.
- Bulk-good deliveries are capped at **1 unit** unless the player currently owns either a
  **Cart** or **Cargo Hover**, in which case the cap is **2 units**.
- Fetch quests only request legal items that can be bought or found in normal play; they
  never require contraband or artifacts.

---

## 11. Economy Loop & Progression

Intended arc:

1. **Scraping (turns 1–10):** Small trades between Starport and adjacent settlements.
   Maybe one prospecting run to Ironpass.  Learn price differentials.

2. **Grind (turns 10–30):** Identify 2–3 reliable routes.  Buy Sturdy Pack + a weapon.
   Push into wilderness for better prospecting.  First combat encounter.

3. **Acceleration (turns 30–60):** Carry higher-value goods (Spices, Refined Metal, Tools).
   Complete 2–3 missions.  Stake at blackjack to flatten variance.  Armor purchase.

4. **Endgame (turns 60–100+):** Gemstone runs in The Barrens; Artifact finds; bounty
   missions.  Net worth climbs toward 10 000 cr.  Return to Starport and pay off the
   impound fee.

Suggested early trade route (seeded as first rumour):
> *"Millhaven's drowning in grain — nobody to sell it to.  Starport market's bone dry."*

---

## 12. UI Layout

Minimum terminal size: **80×24**.  The game detects `SIGWINCH` and redraws on resize.
Colors used when `has_colors()` is true; graceful monochrome fallback.

```
╔══════════════════════════════════════════════════════════════════════════════╗
║  SPACE TRADER                         Turn: 14    Credits:  1,240 cr        ║
╠════════════════════════════════════════╦═══════════════════════════════════╣
║                                        ║  INVENTORY (7/10 units)           ║
║   [Main viewport — 40×16 chars]        ║  Grain x3          3.0 u          ║
║                                        ║  Spices x6         3.0 u          ║
║   Location description, menus,         ║  Furs x1           1.0 u          ║
║   market tables, combat, maps, etc.    ║                                   ║
║                                        ║─────────────────────────────────  ║
║                                        ║  Weapon: Pistol                   ║
║                                        ║  Armor:  Leather Jacket (DR 1)    ║
╠════════════════════════════════════════╩═══════════════════════════════════╣
║  HP: ████████░░  8/10  │  Location: Ironpass (Wilderness)                  ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  [P]rospect  [R]est  [T]ravel  [I]nventory  [?]Help  [Q]uit               ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  > "The ground here is hard and cold.  Somewhere below, ore waits."        ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

### Color Palette

| Element           | Foreground | Background | Attribute  |
|-------------------|-----------|-----------|------------|
| Title bar         | BLACK     | CYAN      | BOLD       |
| Normal text       | WHITE     | BLACK     | —          |
| Profitable item   | GREEN     | BLACK     | —          |
| Danger / illegal  | RED       | BLACK     | —          |
| Restricted item   | YELLOW    | BLACK     | —          |
| Selected row      | BLACK     | WHITE     | —          |
| HP bar            | GREEN→RED | BLACK     | — (% based)|
| Location name     | CYAN      | BLACK     | BOLD       |
| Flavor text       | WHITE     | BLACK     | DIM        |

HP bar color: green above 60%, yellow 30–60%, red below 30%.

### 12.2 Win Screen — Liftoff Animation

Triggered immediately when the player pays the impound fee.  The normal game UI is
cleared and replaced with a full-screen animation followed by a stats panel.

#### Animation Frames

Six frames, each displayed for ~350 ms (last frame held for 2 s).  The ship is a
boxy cargo hauler — unglamorous, functional, yours.

```
FRAME 0 — Engines warming (exhaust glow only)
  ·  ·  *    ·        *    ·   ·      *      ·
     *      ·    ·  *      ·        *
  ·      *         ·    *      ·       *    ·

           ___________________________
          |  ·  ·  ·  ·  ·  ·  ·  ·  |
     _____|___________________________|_____
    |                                       |
    |  F R E E B I R D  — Reg. KR-7712     |
    |_______________________________________|
      [=]       [=]       [=]       [=]
      | |       | |       | |       | |
  ::::|:|::::::::|:|::::::::|:|::::::::|:|::::
  ====[ LAUNCH PAD 4 — PORT VEGA STARPORT ]====
  ~   ~   ~   ~   ~   ~   ~   ~   ~   ~   ~
```

```
FRAME 1 — Lifting off (small exhaust burst)
  ·  ·  *    ·        *    ·   ·      *      ·
     *      ·    ·  *      ·        *
           ___________________________
          |  ·  ·  ·  ·  ·  ·  ·  ·  |
     _____|___________________________|_____
    |                                       |
    |  F R E E B I R D  — Reg. KR-7712     |
    |_______________________________________|
      {*}       {*}       {*}       {*}
    *  }  *   *  }  *   *  }  *   *  }  *
   * * * * * * * * * * * * * * * * * * * *
  ====[ LAUNCH PAD 4 — PORT VEGA STARPORT ]====
  ~   ~   ~   ~   ~   ~   ~   ~   ~   ~   ~
```

```
FRAME 2 — Rising (exhaust plume growing)
  ·  ·  *    ·        *    ·   ·      *      ·
     *      ·    ·  *      ·        *
  ·                                       ·
           ___________________________
          |  ·  ·  ·  ·  ·  ·  ·  ·  |
     _____|___________________________|_____
    |                                       |
    |  F R E E B I R D  — Reg. KR-7712     |
    |_______________________________________|
      \\ //     \\ //     \\ //     \\ //
    *  \*/  *  *  \*/  *  *  \*/  *  *  \*/  *
   *  * | * *  *  * | * *  *  * | * *  *  * | *
  ====[ LAUNCH PAD 4 — PORT VEGA STARPORT ]====
```

```
FRAME 3 — Climbing (ship smaller, plume spreading)
  ·  ·  *    ·        *    ·   ·      *      ·
     *      ·    ·  *      ·        *
           ___________________________
          |  ·  ·  ·  ·  ·  ·  ·  ·  |
     _____|___________________________|_____
    |  F R E E B I R D  — Reg. KR-7712     |
    |_______________________________________|
       | | |     | | |     | | |     | | |
      *  *  *   *  *  *   *  *  *   *  *  *
    *    *    * *    *    * *    *    * *    *
   *  *   *  *   *  *  *   *  *   *  *   *  *
  ====[ LAUNCH PAD 4 — PORT VEGA STARPORT ]====
```

```
FRAME 4 — Departing (ship tiny, exhaust fading)
  ·  ·  *  ___________________________  *      ·
     *    |· · · · · · · · · · · · · ·|    *
  ·  *    |___________________________|  *   ·
       *       | |           | |       *
          *   *   *       *   *   *
       *    *    *    * *    *    *    *
     *   *    *    *    *    *    *  *
  ====[ LAUNCH PAD 4 — PORT VEGA STARPORT ]====
```

```
FRAME 5 — Gone (empty pad, starfield)
  ·     *    ·     *      ·     *    ·     *
     ·     *    ·      *    ·     *     ·
  *    ·    *    ·    *    ·    *    ·    *
     *    ·    *    ·    *    ·    *    ·
  ·     *    ·     *      ·     *    ·     *

  ====[ LAUNCH PAD 4 — PORT VEGA STARPORT ]====
              (the pad is empty)
```

#### Stats Panel (displayed after animation, held until keypress)

```
╔══════════════════════════════════════════════════════════════════════════════╗
║                                                                              ║
║     *    ·   *    ·    *   ·    *    ·   *    ·    *   ·    *    ·    *     ║
║  ·    *    ·    *    ·   *    ·    *    ·   *    ·    *    ·    *    ·    · ║
║                                                                              ║
║     ██   ██  ███████   █████   ███████   █████   ██████  ██   ██    █      ║
║     ██   ██     ██    ██         ██    ██   ██  ██   ██  ██   ██    █      ║
║      ██ ██      ██    ██         ██    ██   ██  ██   ██   ██ ██     █      ║
║       █ ██      ██    ██         ██    ██   ██  ██████      ██      █      ║
║        ██       ██    ██         ██    ██   ██  ██  ██      ██             ║
║        ██     ███████  █████     ██     █████   ██   ██     ██      █      ║
║                                                                              ║
║           F R E E B I R D  H A S  L E F T  S T A R P O R T                 ║
║                                                                              ║
║  ────────────────────────────────────────────────────────────────────────  ║
║                                                                              ║
║    Turns taken   :   47                                                      ║
║    Wealth (cash) :   10 420 Cr                                               ║
║    Cargo value   :    1 380 Cr                                               ║
║    Total wealth  :   11 800 Cr                                               ║
║                                                                              ║
║    SCORE         :   12 270  pts                                             ║
║    (credits + cargo value + turns_survived × 10)                            ║
║                                                                              ║
║  ────────────────────────────────────────────────────────────────────────  ║
║                                                                              ║
║       Fare thee well, trader.  The stars are yours now.                     ║
║                                                                              ║
║                              [ PRESS ANY KEY ]                              ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

Colors: **VICTORY!** rendered in CYAN BOLD; stat labels in WHITE; stat values
in GREEN BOLD; the farewell line in YELLOW; starfield dots in random DIM WHITE.

After keypress, the high-score entry prompt is shown (§15) before returning to the
title screen.

---

## 13. Input Scheme

Single-key commands throughout.  Quantity entry uses `+`/`−` or a typed number.

| Key         | Context           | Action                          |
|-------------|-------------------|---------------------------------|
| Arrow / hjkl| Travel map        | Move cursor between locations   |
| Enter/Space | Most contexts     | Confirm / select                |
| 1–4         | Location          | Travel to listed connected location |
| T           | Location          | Travel menu (alternate)         |
| P           | Wilderness        | Prospect                        |
| R           | Wilderness        | Sleep rough (recover 1 HP, free, risky) |
| S           | Settlement        | Visit Store                     |
| B           | Settlement/Starport | Visit Bar                     |
| C           | Settlement        | Visit Clinic (up to 3 HP, 25 Cr/pt)    |
| H           | Starport          | Visit Hospital (up to 8 HP, 50 Cr/pt)  |
| I           | Anywhere          | Inventory / drop items                          |
| G           | Location w/ drops | Grab dropped cargo from the ground              |
| M           | Starport          | Market                          |
| V           | Anywhere          | Manual save                     |
| X           | Starport          | Pay impound fee (if enough cr)  |
| +/−         | Market/gambling   | Adjust quantity or bet          |
| 0–9         | Market/gambling   | Quick numeric entry             |
| Esc         | Sub-menus         | Back                            |
| Q           | Anywhere          | Quit (with confirmation)        |
| ?           | Anywhere          | Context-sensitive help          |

---

## 14. Save System

One save slot per user.  Save file: `~/.spacetrader.sav`.

```c
typedef struct {
    uint32_t magic;     // 0x53504143  ("SPAC")
    uint16_t version;   // file format version
    uint32_t checksum;  // CRC32 of everything after this field
    game_t   state;     // full game state (player, world, markets, turn count)
} savefile_t;
```

Auto-save on every location arrival.  Manual save: `V` key at any location main menu.
On corrupt/version-mismatch: prompt to start a new game (old file renamed `.bak`).

---

## 15. High Score Table

On death (or victory), the player's score is recorded in `~/.spacetrader.scores`.
Score = `credits_on_hand + cargo_value + (turns_survived × 10)`.
Top 10 scores are displayed on the title screen.

---

## 16. Build & Dependencies

| Dependency | Version | Notes                           |
|------------|---------|---------------------------------|
| C compiler | C99+    | gcc or clang; `-std=c99`        |
| ncurses    | 6.x     | `libncurses-dev` / `ncurses.h`  |
| make       | any     | GNU make preferred              |

No other runtime dependencies.

```
make          # build release binary: ./spacetrader
make debug    # build with -g -DDEBUG
make clean    # remove build artifacts
```

---

## 17. Future / Optional Features (v2+)

- **Crew / companion:** Hire a local guide who adds +10 to wilderness encounter rolls.
- **Faction system:** Traders' Guild (price discounts) vs. Bandit Brotherhood (safe passage).
- **More gambling:** Poker (vs. NPCs), dice games, arm-wrestling.
- **Ship upgrades:** Once you pay off the impound, a v2 could open hyperspace travel.
- **Story missions:** A chain of 5 quests building toward a larger narrative reveal.
- **Permadeath variants:** Ironman (no save mid-game), Sprint (25-turn time limit).

---

## TODO

Implementation gaps to close against this design:

- [ ] Add a full mission loop (job generation, accept/decline, progress tracking, and rewards/fail states).
- [x] Implement bar gambling mini-games (blackjack/roulette) with proper bet controls.
- [ ] Expand encounter flow to include richer non-combat branches (including surrender) across road and wilderness events.
- [x] Implement mule/cart damage semantics from spec (mule-first-hit behavior, cart break/spill handling, and recovery outcomes).
- [ ] Expand combat command set/behaviors to match design details (including surrender and ally/mule interactions if equipped).
- [x] Align save/high-score behavior with spec details: primary file naming/location, corrupt-save backup/rename flow, and score formula parity.
- [ ] Add context-sensitive help overlay on `?` in all major contexts.
- [ ] Support quantity/bet controls per spec in relevant menus (`+`/`−`, `0–9` quick entry where applicable).
- [x] Add terminal resize handling (`SIGWINCH`) and redraw-safe layout updates.
- [x] Refine end-state presentation to match the richer victory/death flow described in this document.
- [ ] Continue refactoring toward the planned module/state-table architecture for encounters, missions, and gambling systems.

---

*End of Design Document — v0.1*
