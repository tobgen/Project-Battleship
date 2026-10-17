# Battleship – terminal multiplayer

Keyboard-only Battleship for the Linux terminal (ncurses), made for kids
playing each other at a LAN evening. One static binary contains both the
server (with an ncurses admin console and a leaderboard) and the client.
The UI is Swedish by default; `--lang en` switches to English.

Svensk version: [README.sv.md](README.sv.md)

## Build

    make
    ./battleship --version      # battleship 1.0.2 (git-rev, built 2026-10-17T15:05:00Z)

The version number lives in the `VERSION` file; the git revision and build
time are stamped in at `make` and also shown top right in client and console.

Produces `./battleship`, statically linked (glibc + ncursesw + tinfo), stripped,
about 1.3 MB. Copy it to any x86-64 Linux box and run it. The only runtime
dependency is a terminfo database (`/usr/share/terminfo` or `/lib/terminfo`),
which every normal Linux install has.

Build requirements on the build machine: `gcc`, `make`, `libncurses-dev`.

## Server

    ./battleship server [--port N] [--seats N] [--leaderboard FILE] [--grace S] [--headless] [--autopair [2|3]] [--lang sv|en]

- `--port`         TCP port, default 4242
- `--seats`        number of fixed seats, default 10 (max 32)
- `--leaderboard`  file the leaderboard is saved to, default `battleship-leaderboard.txt` in the current directory
- `--grace`        seconds a player who lost the connection may come back before the game is aborted, default 90
- `--show-misses`  three-way: also show others' misses from the start (default: hits only)
- `--paused`       start paused (same as pressing `P` right away); `kill -USR1 <pid>` toggles pause from outside
- `--headless`     no console, log to stdout (also used automatically when stdin is not a tty)
- `--autopair`     pair waiting seats automatically in seat order, two or three at a time (useful without an admin)
- `--lang`         `sv` (default) or `en`; the `BATTLESHIP_LANG` environment variable works too

### Admin console

The server shows a table of all seats (client id, player name, state, match,
wins/losses this session), the live matches and the leaderboard. `Tab` moves
focus between the seat table and the leaderboard.

Seat focus:

| Key           | Action                                                 |
|---------------|--------------------------------------------------------|
| Up/Down, j/k  | move the cursor                                        |
| Space         | mark/unmark the seat under the cursor (max three)      |
| p or Enter    | pair the marked seats: two = normal game, three = three-way |
| u             | abort the match of the seat under the cursor           |
| r             | restart the current game (empty boards, no score)      |
| m             | three-way: show/hide others' misses (default hidden)   |
| P             | pause on/off: finished games wait for the next round and auto-pairing stops; manual pairing still works |
| e             | rename the player at the seat (pushed to the client)   |
| x             | kick the seat under the cursor                         |
| Tab           | go to the leaderboard                                  |
| q             | shut down the server                                   |

Leaderboard focus:

| Key           | Action                                                 |
|---------------|--------------------------------------------------------|
| Up/Down, j/k  | move the cursor                                        |
| e or Enter    | rename the row; if the name exists the rows are merged |
| d             | delete the row                                         |
| R             | reload the leaderboard file from disk                  |
| Tab           | back to the seats                                      |

Only seats in the `lobby` state can be paired. **A pair stays together:** when
a game ends, the next game between the same two players starts automatically
(the loser opens), except while the server is paused (`P`): then the result
stays on the players' screens until the leader presses `P` again, and all
waiting rounds start. The pair is only broken when the admin presses `u` or one
of the players disconnects; the players then return to the lobby and can be
paired again.

### Three-way games (three players)

Pairing three seats starts a three-way game. Turns go round A → B → C; each
shot is aimed at an enemy of your choice. The two enemy grids sit side by side
at the top, your own fleet below. `Tab` switches target, and the crosshair also
jumps to the other grid when you run off the edge. The target's title is
highlighted. Each enemy board is one board: hits the other attacker scores
show up too. Their misses are hidden by default (the leader can show them
with `m`). You may always fire at a cell someone else missed; it then counts
as a miss for you too. A player whose
fleet is sunk is out but watches the rest; the last fleet afloat wins (one
win, two losses on the leaderboard). If a player leaves a three-way game in
progress the other two play on, and the next round is a normal game between
them.

### Lost connections

If a client vanishes mid-game (cable, laptop going to sleep, window closed)
the server holds the seat for `--grace` seconds. The client retries every two
seconds on its own, and when it comes back with the same client id it gets the
whole game state back (fleet, shots both ways, whose turn) and play continues.
The opponent sees "waiting for them to come back" meanwhile. If the player
does not return in time the game is aborted and the opponent goes to the
lobby. The console shows such a seat as `away 12s`.

The same client id always means the same player: if a client connects while
the old connection still looks alive (a pulled cable gives no signal), the new
one takes over the seat and the old one is told and stops retrying. The
default id is hostname plus terminal (e.g. `pc-pts3`), so several clients on
one machine get different ids; otherwise set a unique `--id`.

### Leaderboard

The leaderboard counts wins, losses and hit ratio (hits/shots, all time) per
player name (case-insensitive, including åäö) and survives restarts: it is written to the `--leaderboard`
file after every game and loaded when the server starts. The top ten is sent
to every client so the kids see it in the lobby.

Under the leaderboard is **Hit ratio tonight**: hit percentage since the server
started, sorted by percentage with `(hits/shots)` so small samples are obvious.
It updates after every shot and resets when the server restarts. Each player
also sees their own ratio for the current game next to the enemy ship bar
(`Hits 5/12 (42%)`).

The file is plain text with a `# v2` header, one line per player:
`wins<TAB>losses<TAB>hits<TAB>shots<TAB>name`. Older files without hits/shots
are read and upgraded on the next save. It
can be edited by hand with the server stopped, or while running followed by
`R` in the console. Lines with the same name are merged on load.

## Client

    ./battleship client <host> [--id ID] [--port N] [--seat N] [--lang sv|en] [--cell 1-3] [--logo] [--sound] [--flash]
    ./battleship client --test        # against one robot
    ./battleship client --test3       # three-way against two robots

- `host`      IPv4 address of the server (or `localhost`)
- `--id`      client id the admin sees in the console, e.g. `pc-3` (default: hostname+terminal; `A-Z a-z 0-9 _ -`, max 15). `--client` is the same.
- `--name`    preset player name; without it the player types a name at startup
- `--port`    server port, default 4242
- `--seat`    request a specific seat; without it you get the first free seat
- `--lang`    `sv` (default) or `en`
- `--cell`    cell size in rows (width is double); without it the largest that fits is used
- `--logo`    show a big SCOUT banner under the leaderboard
- `--sound`   beep when it becomes your turn
- `--flash`   flash the screen when it becomes your turn
- `--test`    test mode: starts a local server and a robot opponent in the background so the
              whole flow (name, placement, battle, next game, leaderboard) can be tried on a
              single machine. The test-mode leaderboard is temporary and removed on exit.

At startup the client asks **What is your name?** The player types a name
(åäö and spaces are fine, max 24 bytes) and presses Enter. After every game,
when Enter is pressed for the next one, the client asks **Who plays the next
game?** with an empty field: whoever is playing has to type their name before
the next game starts. In the lobby and during placement `n` changes the name.
The name is what the opponent sees and what the leaderboard counts; the client
id is only for the admin.

The screen shows enemy waters on top and your own fleet below, with the
leaderboard to the right when there is room (and the SCOUT banner under it with `--logo`). Water is drawn as `≈`, ships as
`█`, hits as `╳` and misses as `○`. With a terminal of at least 55 rows each
cell is 2x4 characters (looks square), otherwise 1x2. Minimum terminal: 36
rows x 25 columns; about 90 columns recommended. Requires a UTF-8 terminal.

### Keys

Keys are case-insensitive (Caps Lock does not matter), and Space and Enter
do the same thing during play.

Placement phase (after the admin pairs you):

| Key              | Action                                  |
|------------------|-----------------------------------------|
| Arrows / hjkl    | move the ship ghost                     |
| r                | rotate                                  |
| Space/Enter      | place the ship                          |
| Backspace        | undo the last placed ship               |
| a                | auto-place the remaining ships          |
| n                | change name                             |

The next ship appears on a free, valid spot, and the ship is always kept
inside the board when moving or rotating.

Battle phase:

| Key              | Action                                  |
|------------------|-----------------------------------------|
| Arrows / hjkl    | move the crosshair on the enemy grid    |
| Space / Enter    | fire                                    |

The crosshair is a black block on bright yellow on your turn and white on
dark grey while the opponent fires; the status bar is green on your turn and
dark grey while waiting. Colours are fixed (the xterm 256-colour palette) and
do not follow the terminal theme. With
`--sound` and/or `--flash` the client also beeps or flashes when the turn
becomes yours (off by default).

Troubleshooting: `BATTLESHIP_DEBUG=/tmp/bs.log ./battleship client ...` writes
every message from the server to that file.

After a game the final boards stay on screen; `Enter` goes via the name
question to placement for the next game against the same opponent. The client
has no quit key (so the kids cannot close it by accident); a leader quits with
`Ctrl+C`, which shuts down cleanly.

Fleet: Carrier 5, Battleship 4, Cruiser 3, Submarine 3, Destroyer 2. Ships may
not touch each other, not even diagonally; there must be at least one cell of
water between them. This applies to manual placement, auto-placement and is
also enforced by the server. Turns alternate after every shot. First player
to sink all five ships wins.

## Projector view

    ./battleship board <host> [--port N] [--logo] [--lang sv|en]

A read-only view for a projector or big screen: the leaderboard, live games
(who plays whom, who is to fire, shot count) and the latest results.
Reconnects automatically if the server restarts. Quit with `Ctrl+C`.

## Protocol

Plain text, one newline-terminated message per line, so it can be tested with
`nc`. Client to server: `HELLO <seat|0> <client id>`, `NAME <name>`,
`FLEET r,c,h;...`, `FIRE r c [target seat]`, `BYE`; the projector view sends `WATCH <id>`.
Server to client: `WELCOME`, `NAMEOK`, `OPPNAME`, `LOBBY`, `MATCH`/`MATCH3`, `FLEETOK`,
`START`, `TURN`, `SHOT`, `INCOMING`, `OSHOT`, `OUT`, `GAMEOVER`, `OPPLEFT`, `ABORT`,
`RESTART`, `LBCLEAR`, `LB <w> <l> <name>`, `OPPWAIT`, `OPPBACK`,
`RESUME`/`RESUME3`/`MYFLEET`/`MYSHOTS`/`ENEMYSHOTS`/`ENEMYSUNK` (resumed game),
`MCLEAR`/`M`/`MEND`/`EV` (projector view), `ERR`.
The server owns all game state and validates every move; a client never
receives the opponent's ship positions.
