#include "i18n.h"

#include <stdlib.h>
#include <string.h>

int lang = LANG_SV;

static const char *const STR[2][S_COUNT] = {
    [LANG_SV] = {
        [S_SHIP_0] = "Hangarfartyg",
        [S_SHIP_1] = "Slagskepp",
        [S_SHIP_2] = "Kryssare",
        [S_SHIP_3] = "Ubåt",
        [S_SHIP_4] = "Jagare",
        [S_SHIP_POSS_0] = "ditt Hangarfartyg",
        [S_SHIP_POSS_1] = "ditt Slagskepp",
        [S_SHIP_POSS_2] = "din Kryssare",
        [S_SHIP_POSS_3] = "din Ubåt",
        [S_SHIP_POSS_4] = "din Jagare",

        [S_YOUR_FLEET]      = "Din flotta",
        [S_ENEMY_WATERS]    = "Fiendens vatten",
        [S_SHIPS]           = "Skepp ",
        [S_TOO_SMALL]       = "Terminalen är för liten (behöver %dx%d, har %dx%d)",
        [S_SEAT_ME]         = "Plats %d: %s",
        [S_SEAT_OPP]        = "mot  Plats %d: %s",
        [S_SEAT_OPP2]       = "och  Plats %d: %s",
        [S_VS]              = " mot ",
        [S_THE_ENEMY]       = "fienden",
        [S_OUT_TAG]         = " (utslagen)",
        [S_KEYS_DEFAULT]    = "",
        [S_KEYS_LOBBY]      = "n byt namn",
        [S_KEYS_PLACING]    = "Pilar/hjkl flytta  r rotera  Mellanslag/Enter placera  Backsteg ångra  a auto-placera  n byt namn",
        [S_KEYS_PLAYING]    = "Pilar/hjkl sikta  Mellanslag/Enter skjut",
        [S_KEYS_PLAYING3]   = "Pilar/hjkl sikta  Tab byt motståndare  Mellanslag/Enter skjut",
        [S_CONNECTED]       = "Ansluten. Du har plats %d av %d.",
        [S_LOBBY_WAIT]      = "I lobbyn. Väntar på att admin ska para ihop dig med en motståndare...",
        [S_MATCHED]         = "Matchad mot %s (plats %d).",
        [S_MATCHED3]        = "Trekamp! Du möter %s och %s.",
        [S_PLACE_SHIP]      = "Placera din %s (%d). r roterar, Mellanslag placerar, a auto-placerar.",
        [S_FLEET_ACCEPTED]  = "Flottan godkänd. Väntar på att de andra ska placera klart...",
        [S_BATTLE_BEGINS]   = "Striden börjar! %s är först.",
        [S_YOU]             = "Du",
        [S_YOUR_TURN]       = "DIN TUR - sikta och skjut!",
        [S_YOUR_TURN3]      = "DIN TUR - välj motståndare (Tab) och skjut!",
        [S_SOMEONE_SUNK]    = "%s sänkte %ss %s!",
        [S_OTHER_FIRED]     = "%s sköt mot %ss %c%d: %s",
        [S_YOU_ARE_OUT]     = "Du är utslagen! Titta på resten av matchen.",
        [S_ENEMY_OUT]       = "%s är utslagen!",
        [S_WAIT_FIRE]       = "Väntar på att %s ska skjuta...",
        [S_YOU_FIRED_SUNK]  = "Du sköt mot %c%d: TRÄFF och SÄNKTE %ss %s!",
        [S_YOU_FIRED]       = "Du sköt mot %c%d: %s",
        [S_HIT_EXCL]        = "TRÄFF!",
        [S_MISS_DOT]        = "bom.",
        [S_OPP_FIRED_SUNK]  = "%s sköt mot %c%d och SÄNKTE %s!",
        [S_OPP_FIRED]       = "%s sköt mot %c%d: %s",
        [S_HIT_DOT]         = "träff.",
        [S_VICTORY]         = "*** SEGER! Du sänkte hela fiendens flotta. ***",
        [S_DEFEAT]          = "*** FÖRLUST. %s vann matchen. ***",
        [S_YOU_WON]         = "Du vann!",
        [S_YOU_LOST]        = "Du förlorade.",
        [S_NEXT_GAME]       = "%s Tryck Enter för nästa match mot %s.",
        [S_KEYS_GAMEOVER]   = "Enter nästa match (skriv vem som spelar)",
        [S_OPP_LEFT]        = "%s lämnade spelet.",
        [S_ADMIN_ABORT]     = "Admin avbröt matchen.",
        [S_SERVER_SAYS]     = "Servern: %s",
        [S_REJECTED]        = "Nekad: %s",
        [S_UNKNOWN_MSG]     = "Okänt meddelande: %s",
        [S_CANT_PLACE]      = "Kan inte placera %s där.",
        [S_SENDING_FLEET]   = "Skickar flottan...",
        [S_NOT_YOUR_TURN]   = "Inte din tur.",
        [S_ALREADY_FIRED]   = "Du har redan skjutit mot %c%d.",
        [S_CONNECTING]      = "Ansluter...",
        [S_DISCONNECTED]    = "Frånkopplad från servern. Säg till en ledare.",
        [S_CONN_LOST]       = "Anslutningen bröts.",
        [S_CONNECT_FAILED]  = "anslutning till %s:%d misslyckades: %s\n",
        [S_NAME_PROMPT]     = "Vad heter du?",
        [S_NAME_HINT]       = "Skriv ditt namn och tryck Enter.",
        [S_NAME_ESC]        = "Esc för att behålla ditt gamla namn.",
        [S_NAME_CLIENT]     = "Klient: %s   Plats %d",
        [S_NAME_SET]        = "Hej %s! Lycka till.",
        [S_OPP_RENAMED]     = "%s heter nu %s.",
        [S_NAME_NEXT_PROMPT]= "Vem spelar nästa match?",
        [S_NAME_NEXT_HINT]  = "Skriv namnet på den som spelar nu och tryck Enter.",
        [S_RESTARTED]       = "Admin startade om matchen. Placera dina skepp igen.",
        [S_LB_TITLE]        = "Topplista",
        [S_LB_ROW]          = "%2d. %s %3d V %3d F %3d%%",
        [S_LB_EMPTY]        = "(inga matcher spelade än)",
        [S_SB_TITLE]        = "Träffsäkerhet ikväll",
        [S_SB_ROW]          = "%2d. %s %3d%% (%d/%d)",
        [S_STAT]            = "Träffar %d/%d (%d%%)",
        [S_TEST_MODE]       = "TESTLÄGE: lokal server och robotmotståndare, ingen riktig server behövs.",
        [S_TEST_FAILED]     = "kunde inte starta testläget (ingen ledig port?)\n",
        [S_RECONNECTING]    = "Anslutningen bröts. Försöker ansluta igen...",
        [S_RECONNECTED]     = "Ansluten igen.",
        [S_RESUMED]         = "Matchen fortsätter där den var.",
        [S_OPP_WAIT]        = "%s tappade anslutningen. Väntar på att hen kommer tillbaka...",
        [S_OPP_BACK]        = "%s är tillbaka!",
        [S_TAKEN_OVER]      = "En annan klient med samma klient-id anslöt. Starta om med --client <unikt namn>.",
        [S_VIEW_TITLE]      = "TOPPLISTA",
        [S_VIEW_MATCHES]    = "PÅGÅENDE MATCHER",
        [S_VIEW_FEED]       = "SENASTE",
        [S_VIEW_WAITING]    = "väntar på servern...",
        [S_VIEW_NONE]       = "(inga matcher just nu)",
        [S_VIEW_MATCH]      = "%s  mot  %s",
        [S_VIEW_SHOOTS]     = "%s skjuter  (%d skott)",
        [S_VIEW_PLACING]    = "placerar skepp",
        [S_VIEW_AWAY]       = "väntar på spelare som tappat anslutningen",
        [S_BOARD_USAGE]     =
            "användning: battleship board <värd> [--port N] [--logo] [--lang sv|en]\n"
            "  Projektorvy: visar topplistan, pågående matcher och senaste resultat. Avsluta med Ctrl+C.\n"
            "  --port  serverport (standard %d)\n  --logo  visa SCOUT-bannern\n",
        [S_CLIENT_USAGE]    =
            "användning: battleship client <värd> [--client ID] [--port N] [--seat N] [--lang sv|en] [--cell 1-3] [--logo]\n"
            "            battleship client 192.168.1.10 --client dator-3 --seat 3\n"
            "            battleship client --test        (spela mot en robot utan server)\n"
            "            battleship client --test3       (trekamp mot två robotar)\n"
            "  --client  klient-id som admin ser i serverkonsolen (standard: datornamn+terminal, t.ex. pc-pts3)\n"
            "  --id      samma som --client; sätt ett unikt id per klient om flera körs på samma dator\n"
            "  --name    förifyllt spelarnamn; annars får spelaren skriva sitt namn vid start\n"
            "  --port  serverport (standard %d)\n"
            "  --seat  begär en viss plats (standard: första lediga)\n"
            "  --lang  språk, sv eller en (standard sv, eller BATTLESHIP_LANG)\n"
            "  --cell  rutstorlek i rader, 1-3 (standard: automatiskt efter terminalen)\n"
            "  --test  testläge: startar en lokal server och en robotmotståndare på den här datorn\n"
            "  --test3 som --test men trekamp med två robotar\n"
            "  --logo  visa SCOUT-bannern under topplistan\n"
            "  --sound pip när det blir din tur\n"
            "  --flash blinka skärmen när det blir din tur\n",

        [S_SRV_TITLE]       = " BATTLESHIP-SERVER  port %d  platser %d  %s",
        [S_SRV_AUTOPAIR]    = "[autopar]",
        [S_SRV_HEADER]      = "Plats Klient       Namn             Status   Match   V/F  Mark",
        [S_SRV_MATCHES]     = "Matcher",
        [S_SRV_NONE]        = "(inga)",
        [S_SRV_PLACING]     = "%s placerar",
        [S_SRV_TURN]        = "%s: tur %d, %d skott",
        [S_SRV_HELD]        = "%s: klar, väntar på P",
        [S_SRV_PAUSED]      = "PAUS: inga nya omgångar",
        [S_SRV_MISSES_ON]   = "trekamp: bommar visas (m)",
        [S_SRV_MISSES_OFF]  = "trekamp: bommar dolda (m)",
        [S_LOG_MISSES_ON]   = "Trekamp: andras bommar visas nu",
        [S_LOG_MISSES_OFF]  = "Trekamp: andras bommar döljs nu",
        [S_LOG_PAUSE_ON]    = "Paus: avslutade matcher väntar, ingen autoparning",
        [S_LOG_PAUSE_OFF]   = "Paus av: väntande omgångar startar",
        [S_HOLD]            = "Matchen är slut. Väntar på att ledaren startar nästa omgång...",
        [S_SRV_KEYS]        = " Mellanslag markera  p para  P paus  m bommar  u bryt  r starta om  e byt namn  x sparka  Tab topplista  q avsluta",
        [S_SRV_KEYS_LB]     = " Upp/Ner  e byt namn  d ta bort  R läs om filen  Tab platser  q avsluta",
        [S_SRV_LB]          = "Topplista",
        [S_SRV_LB_ROW]      = "%2d. %s %3d V %3d F %3d%%",
        [S_SRV_PROMPT_SEAT] = "Nytt namn för spelaren (Enter sparar, Esc avbryter):",
        [S_SRV_PROMPT_LB]   = "Nytt namn i topplistan (Enter sparar, Esc avbryter):",
        [S_ST_EMPTY]        = "tom",
        [S_ST_LOBBY]        = "lobby",
        [S_ST_PLACING]      = "placerar",
        [S_ST_READY]        = "redo",
        [S_ST_PLAYING]      = "spelar",
        [S_ST_GONE]         = "borta",
        [S_LOG_LISTENING]   = "Lyssnar på port %d med %d platser",
        [S_LOG_SEAT_TAKEN]  = "Plats %d togs av klient %s",
        [S_LOG_LEFT]        = "Plats %d (%s) lämnade: %s",
        [S_LOG_PAIRED]      = "Ny match: %s",
        [S_LOG_ABORTED]     = "Bröt match %s",
        [S_LOG_STARTED]     = "Match startad: %s, plats %d är först",
        [S_LOG_REMATCH]     = "Ny omgång: %s (samma spelare)",
        [S_LOG_RESTART]     = "Startade om matchen %s",
        [S_LOG_FLEET_READY] = "Plats %d (%s) flotta redo",
        [S_LOG_WON]         = "%s vann (%s) efter %d skott",
        [S_LOG_OUT]         = "Plats %d (%s) är utslagen",
        [S_LOG_NOT_LOBBY]   = "Plats %d är inte i lobbyn",
        [S_LOG_MARK_TWO]    = "Markera två eller tre platser i lobbyn först",
        [S_LOG_CANT_PAIR]   = "Kan inte para de platserna",
        [S_LOG_NOT_IN_MATCH]= "Plats %d är inte i någon match",
        [S_LOG_NOT_NOW]     = "Går inte att byta namn under pågående spel",
        [S_LOG_NAME]        = "Plats %d (%s) heter nu %s",
        [S_LOG_LB_SAVE_FAIL]= "Kunde inte spara topplistan till %s: %s",
        [S_LOG_LB_LOADED]   = "Läste %d spelare från %s",
        [S_LOG_LB_DELETED]  = "Tog bort %s från topplistan",
        [S_LOG_LB_RENAMED]  = "Topplistan: %s heter nu %s",
        [S_WHY_BYE]         = "hejdå",
        [S_WHY_DISCONNECTED]= "frånkopplad",
        [S_WHY_KICKED]      = "sparkad",
        [S_WHY_TIMEOUT]     = "kom inte tillbaka",
        [S_LOG_GONE]        = "Plats %d (%s) tappade anslutningen, platsen hålls i %d s",
        [S_LOG_RESUMED]     = "Plats %d (%s) är tillbaka, matchen fortsätter",
        [S_LOG_REPLACED]    = "Plats %d (%s) anslöt igen, gamla anslutningen stängd",
        [S_LOG_VIEWER]      = "Projektorvy ansluten",
        [S_EV_PAIRED]       = "%s möter %s",
        [S_EV_WIN]          = "%s vann (%s) efter %d skott",
        [S_SRV_USAGE]       =
            "användning: battleship server [--port N] [--seats N] [--leaderboard FIL] [--grace S] [--headless] [--autopair [2|3]] [--paused] [--show-misses] [--lang sv|en]\n"
            "  --port      TCP-port (standard %d)\n"
            "  --seats     antal fasta platser, 2-%d (standard 10)\n"
            "  --headless  ingen adminkonsol, logga till stdout\n"
            "  --paused    starta pausad: inga nya omgångar förrän admin trycker P\n"
            "  --show-misses  trekamp: visa även andras bommar (standard: bara träffar)\n"
            "  --autopair  para väntande platser automatiskt, två (standard) eller tre åt gången\n"
            "  --lang      språk, sv eller en (standard sv, eller BATTLESHIP_LANG)\n"
            "  --leaderboard  fil där topplistan sparas (standard %s)\n"
            "  --grace     sekunder en spelare som tappat anslutningen får komma tillbaka (standard %d)\n",
        [S_SRV_SEATS_RANGE] = "antal platser måste vara mellan 2 och %d\n",

        [S_MAIN_USAGE]      =
            "Battleship - sänka skepp i terminalen\n\n"
            "  battleship server [--port N] [--seats N] [--leaderboard FIL] [--headless] [--autopair] [--lang sv|en]\n"
            "  battleship client <värd> [--client ID] [--port N] [--seat N] [--lang sv|en] [--cell 1-3] [--logo]\n"
            "  battleship client --test\n"
            "  battleship board <värd> [--port N] [--logo]        (projektorvy)\n\n"
            "  battleship server --help / battleship client --help visar alla flaggor.\n"
            "  battleship --version visar vilken version som körs.\n",
    },
    [LANG_EN] = {
        [S_SHIP_0] = "Carrier",
        [S_SHIP_1] = "Battleship",
        [S_SHIP_2] = "Cruiser",
        [S_SHIP_3] = "Submarine",
        [S_SHIP_4] = "Destroyer",
        [S_SHIP_POSS_0] = "your Carrier",
        [S_SHIP_POSS_1] = "your Battleship",
        [S_SHIP_POSS_2] = "your Cruiser",
        [S_SHIP_POSS_3] = "your Submarine",
        [S_SHIP_POSS_4] = "your Destroyer",

        [S_YOUR_FLEET]      = "Your fleet",
        [S_ENEMY_WATERS]    = "Enemy waters",
        [S_SHIPS]           = "Ships ",
        [S_TOO_SMALL]       = "Terminal too small (need %dx%d, have %dx%d)",
        [S_SEAT_ME]         = "Seat %d: %s",
        [S_SEAT_OPP]        = "vs  Seat %d: %s",
        [S_SEAT_OPP2]       = "and  Seat %d: %s",
        [S_VS]              = " vs ",
        [S_THE_ENEMY]       = "the enemy",
        [S_OUT_TAG]         = " (out)",
        [S_KEYS_DEFAULT]    = "",
        [S_KEYS_LOBBY]      = "n change name",
        [S_KEYS_PLACING]    = "Arrows/hjkl move  r rotate  Space/Enter place  Backspace undo  a auto-place  n change name",
        [S_KEYS_PLAYING]    = "Arrows/hjkl aim  Space/Enter fire",
        [S_KEYS_PLAYING3]   = "Arrows/hjkl aim  Tab switch enemy  Space/Enter fire",
        [S_CONNECTED]       = "Connected. You have seat %d of %d.",
        [S_LOBBY_WAIT]      = "In the lobby. Waiting for the admin to pair you with an opponent...",
        [S_MATCHED]         = "Matched against %s (seat %d).",
        [S_MATCHED3]        = "Three-way battle! You face %s and %s.",
        [S_PLACE_SHIP]      = "Place your %s (%d). r rotates, Space places, a auto-places.",
        [S_FLEET_ACCEPTED]  = "Fleet accepted. Waiting for the others to finish placing...",
        [S_BATTLE_BEGINS]   = "Battle begins! %s opens.",
        [S_YOU]             = "You",
        [S_YOUR_TURN]       = "YOUR TURN - aim and fire!",
        [S_YOUR_TURN3]      = "YOUR TURN - pick an enemy (Tab) and fire!",
        [S_SOMEONE_SUNK]    = "%s sank %s's %s!",
        [S_OTHER_FIRED]     = "%s fired at %s's %c%d: %s",
        [S_YOU_ARE_OUT]     = "You are out! Watch the rest of the game.",
        [S_ENEMY_OUT]       = "%s is out!",
        [S_WAIT_FIRE]       = "Waiting for %s to fire...",
        [S_YOU_FIRED_SUNK]  = "You fired at %c%d: HIT and SUNK %s's %s!",
        [S_YOU_FIRED]       = "You fired at %c%d: %s",
        [S_HIT_EXCL]        = "HIT!",
        [S_MISS_DOT]        = "miss.",
        [S_OPP_FIRED_SUNK]  = "%s fired at %c%d and SUNK %s!",
        [S_OPP_FIRED]       = "%s fired at %c%d: %s",
        [S_HIT_DOT]         = "hit.",
        [S_VICTORY]         = "*** VICTORY! You sank the whole enemy fleet. ***",
        [S_DEFEAT]          = "*** DEFEAT. %s won the game. ***",
        [S_YOU_WON]         = "You won!",
        [S_YOU_LOST]        = "You lost.",
        [S_NEXT_GAME]       = "%s Press Enter for the next game against %s.",
        [S_KEYS_GAMEOVER]   = "Enter next game (type who plays)",
        [S_OPP_LEFT]        = "%s left the game.",
        [S_ADMIN_ABORT]     = "The admin aborted the match.",
        [S_SERVER_SAYS]     = "Server: %s",
        [S_REJECTED]        = "Rejected: %s",
        [S_UNKNOWN_MSG]     = "Unknown message: %s",
        [S_CANT_PLACE]      = "Can't place the %s there.",
        [S_SENDING_FLEET]   = "Sending fleet...",
        [S_NOT_YOUR_TURN]   = "Not your turn.",
        [S_ALREADY_FIRED]   = "Already fired at %c%d.",
        [S_CONNECTING]      = "Connecting...",
        [S_DISCONNECTED]    = "Disconnected from server. Tell a leader.",
        [S_CONN_LOST]       = "Connection lost.",
        [S_CONNECT_FAILED]  = "connect to %s:%d failed: %s\n",
        [S_NAME_PROMPT]     = "What is your name?",
        [S_NAME_HINT]       = "Type your name and press Enter.",
        [S_NAME_ESC]        = "Esc to keep your old name.",
        [S_NAME_CLIENT]     = "Client: %s   Seat %d",
        [S_NAME_SET]        = "Hi %s! Good luck.",
        [S_OPP_RENAMED]     = "%s is now called %s.",
        [S_NAME_NEXT_PROMPT]= "Who plays the next game?",
        [S_NAME_NEXT_HINT]  = "Type the name of whoever is playing now and press Enter.",
        [S_RESTARTED]       = "The admin restarted the game. Place your ships again.",
        [S_LB_TITLE]        = "Leaderboard",
        [S_LB_ROW]          = "%2d. %s %3d W %3d L %3d%%",
        [S_LB_EMPTY]        = "(no games played yet)",
        [S_SB_TITLE]        = "Hit ratio tonight",
        [S_SB_ROW]          = "%2d. %s %3d%% (%d/%d)",
        [S_STAT]            = "Hits %d/%d (%d%%)",
        [S_TEST_MODE]       = "TEST MODE: local server and robot opponent, no real server needed.",
        [S_TEST_FAILED]     = "could not start test mode (no free port?)\n",
        [S_RECONNECTING]    = "Connection lost. Trying to reconnect...",
        [S_RECONNECTED]     = "Connected again.",
        [S_RESUMED]         = "The game continues where it was.",
        [S_OPP_WAIT]        = "%s lost the connection. Waiting for them to come back...",
        [S_OPP_BACK]        = "%s is back!",
        [S_TAKEN_OVER]      = "Another client connected with the same client id. Restart with --client <unique name>.",
        [S_VIEW_TITLE]      = "LEADERBOARD",
        [S_VIEW_MATCHES]    = "LIVE GAMES",
        [S_VIEW_FEED]       = "LATEST",
        [S_VIEW_WAITING]    = "waiting for the server...",
        [S_VIEW_NONE]       = "(no games right now)",
        [S_VIEW_MATCH]      = "%s  vs  %s",
        [S_VIEW_SHOOTS]     = "%s to fire  (%d shots)",
        [S_VIEW_PLACING]    = "placing ships",
        [S_VIEW_AWAY]       = "waiting for a player who lost the connection",
        [S_BOARD_USAGE]     =
            "usage: battleship board <host> [--port N] [--logo] [--lang sv|en]\n"
            "  Projector view: leaderboard, live games and latest results. Quit with Ctrl+C.\n"
            "  --port  server port (default %d)\n  --logo  show the SCOUT banner\n",
        [S_CLIENT_USAGE]    =
            "usage: battleship client <host> [--client ID] [--port N] [--seat N] [--lang sv|en] [--cell 1-3] [--logo]\n"
            "       battleship client 192.168.1.10 --client pc-3 --seat 3\n"
            "       battleship client --test        (play a robot without a server)\n"
            "       battleship client --test3       (three-way game against two robots)\n"
            "  --client  client id the admin sees in the server console (default: hostname+terminal, e.g. pc-pts3)\n"
            "  --id      same as --client; give each client a unique id when several run on one machine\n"
            "  --name    preset player name; otherwise the player types it at startup\n"
            "  --port  server port (default %d)\n"
            "  --seat  request a specific seat number (default: first free)\n"
            "  --lang  language, sv or en (default sv, or BATTLESHIP_LANG)\n"
            "  --cell  cell size in rows, 1-3 (default: automatic from terminal size)\n"
            "  --test  test mode: starts a local server and a robot opponent on this machine\n"
            "  --test3 like --test but a three-way game with two robots\n"
            "  --logo  show the SCOUT banner under the leaderboard\n"
            "  --sound beep when it becomes your turn\n"
            "  --flash flash the screen when it becomes your turn\n",

        [S_SRV_TITLE]       = " BATTLESHIP SERVER  port %d  seats %d  %s",
        [S_SRV_AUTOPAIR]    = "[autopair]",
        [S_SRV_HEADER]      = "Seat Client       Name             State    Match   W/L  Mark",
        [S_SRV_MATCHES]     = "Matches",
        [S_SRV_NONE]        = "(none)",
        [S_SRV_PLACING]     = "%s placing",
        [S_SRV_TURN]        = "%s: turn %d, %d shots",
        [S_SRV_HELD]        = "%s: finished, waiting for P",
        [S_SRV_PAUSED]      = "PAUSED: no new rounds",
        [S_SRV_MISSES_ON]   = "3-player: misses shown (m)",
        [S_SRV_MISSES_OFF]  = "3-player: misses hidden (m)",
        [S_LOG_MISSES_ON]   = "3-player: others' misses are now shown",
        [S_LOG_MISSES_OFF]  = "3-player: others' misses are now hidden",
        [S_LOG_PAUSE_ON]    = "Paused: finished games wait, no auto-pairing",
        [S_LOG_PAUSE_OFF]   = "Resumed: waiting rounds start",
        [S_HOLD]            = "Game over. Waiting for the leader to start the next round...",
        [S_SRV_KEYS]        = " Space mark  p pair  P pause  m misses  u unpair  r restart  e rename  x kick  Tab leaderboard  q quit",
        [S_SRV_KEYS_LB]     = " Up/Down  e rename  d delete  R reload file  Tab seats  q quit",
        [S_SRV_LB]          = "Leaderboard",
        [S_SRV_LB_ROW]      = "%2d. %s %3d W %3d L %3d%%",
        [S_SRV_PROMPT_SEAT] = "New name for the player (Enter saves, Esc cancels):",
        [S_SRV_PROMPT_LB]   = "New name on the leaderboard (Enter saves, Esc cancels):",
        [S_ST_EMPTY]        = "empty",
        [S_ST_LOBBY]        = "lobby",
        [S_ST_PLACING]      = "placing",
        [S_ST_READY]        = "ready",
        [S_ST_PLAYING]      = "playing",
        [S_ST_GONE]         = "away",
        [S_LOG_LISTENING]   = "Listening on port %d with %d seats",
        [S_LOG_SEAT_TAKEN]  = "Seat %d taken by client %s",
        [S_LOG_LEFT]        = "Seat %d (%s) left: %s",
        [S_LOG_PAIRED]      = "New match: %s",
        [S_LOG_ABORTED]     = "Aborted match %s",
        [S_LOG_STARTED]     = "Match started: %s, seat %d opens",
        [S_LOG_REMATCH]     = "Rematch: %s (same players)",
        [S_LOG_RESTART]     = "Restarted the game %s",
        [S_LOG_FLEET_READY] = "Seat %d (%s) fleet ready",
        [S_LOG_WON]         = "%s won (%s) after %d shots",
        [S_LOG_OUT]         = "Seat %d (%s) is out",
        [S_LOG_NOT_LOBBY]   = "Seat %d is not in lobby",
        [S_LOG_MARK_TWO]    = "Mark two or three lobby seats first",
        [S_LOG_CANT_PAIR]   = "Cannot pair those seats",
        [S_LOG_NOT_IN_MATCH]= "Seat %d is not in a match",
        [S_LOG_NOT_NOW]     = "Cannot rename during a game",
        [S_LOG_NAME]        = "Seat %d (%s) is now called %s",
        [S_LOG_LB_SAVE_FAIL]= "Could not save the leaderboard to %s: %s",
        [S_LOG_LB_LOADED]   = "Loaded %d players from %s",
        [S_LOG_LB_DELETED]  = "Removed %s from the leaderboard",
        [S_LOG_LB_RENAMED]  = "Leaderboard: %s is now %s",
        [S_WHY_BYE]         = "bye",
        [S_WHY_DISCONNECTED]= "disconnected",
        [S_WHY_KICKED]      = "kicked",
        [S_WHY_TIMEOUT]     = "did not come back",
        [S_LOG_GONE]        = "Seat %d (%s) lost the connection, seat held for %d s",
        [S_LOG_RESUMED]     = "Seat %d (%s) is back, game continues",
        [S_LOG_REPLACED]    = "Seat %d (%s) reconnected, old connection closed",
        [S_LOG_VIEWER]      = "Projector view connected",
        [S_EV_PAIRED]       = "%s meets %s",
        [S_EV_WIN]          = "%s won (%s) after %d shots",
        [S_SRV_USAGE]       =
            "usage: battleship server [--port N] [--seats N] [--leaderboard FILE] [--grace S] [--headless] [--autopair [2|3]] [--paused] [--show-misses] [--lang sv|en]\n"
            "  --port      TCP port (default %d)\n"
            "  --seats     number of fixed seats, 2-%d (default 10)\n"
            "  --headless  no admin console, log to stdout\n"
            "  --paused    start paused: no new rounds until the admin presses P\n"
            "  --show-misses  3-player: also show others' misses (default: hits only)\n"
            "  --autopair  pair waiting seats automatically, two (default) or three at a time\n"
            "  --lang      language, sv or en (default sv, or BATTLESHIP_LANG)\n"
            "  --leaderboard  file the leaderboard is saved to (default %s)\n"
            "  --grace     seconds a player who lost the connection may come back (default %d)\n",
        [S_SRV_SEATS_RANGE] = "seats must be between 2 and %d\n",

        [S_MAIN_USAGE]      =
            "Battleship - terminal multiplayer\n\n"
            "  battleship server [--port N] [--seats N] [--leaderboard FILE] [--headless] [--autopair] [--lang sv|en]\n"
            "  battleship client <host> [--client ID] [--port N] [--seat N] [--lang sv|en] [--cell 1-3] [--logo]\n"
            "  battleship client --test\n"
            "  battleship board <host> [--port N] [--logo]        (projector view)\n\n"
            "  battleship server --help / battleship client --help list every flag.\n"
            "  battleship --version shows which build is running.\n",
    },
};

void i18n_init(const char *arg)
{
    const char *v = arg ? arg : getenv("BATTLESHIP_LANG");
    if (v && (strncmp(v, "en", 2) == 0 || strncmp(v, "EN", 2) == 0)) lang = LANG_EN;
    else lang = LANG_SV;
}

const char *tr(int id)
{
    if (id < 0 || id >= S_COUNT) return "?";
    const char *s = STR[lang][id];
    return s ? s : (STR[LANG_EN][id] ? STR[LANG_EN][id] : "?");
}
