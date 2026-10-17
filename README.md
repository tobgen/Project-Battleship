# Battleship – sänka skepp i terminalen

Sänka skepp för Linux-terminalen, enbart tangentbord (ncurses), gjort för att
barn ska kunna spela mot varandra på en LAN-kväll. En statisk binär innehåller
både servern (med en ncurses-adminkonsol och topplista) och klienten.
Gränssnittet är på svenska som standard; `--lang en` ger engelska.

English version: [README.en.md](README.en.md)

## Bygga

    make
    ./battleship --version      # battleship 1.0.2 (git-rev, built 2026-10-17T15:05:00Z)

Versionsnumret står i filen `VERSION`; git-revision och byggtid stämplas in
vid `make` och visas även uppe till höger i klient och konsol.

Ger `./battleship`, statiskt länkad (glibc + ncursesw + tinfo), strippad,
cirka 1,3 MB. Kopiera den till valfri x86-64-Linux och kör. Enda
körtidsberoendet är en terminfo-databas (`/usr/share/terminfo` eller
`/lib/terminfo`), som alla normala Linux-installationer har.

Byggkrav på byggmaskinen: `gcc`, `make`, `libncurses-dev`.

## Server

    ./battleship server [--port N] [--seats N] [--leaderboard FIL] [--grace S] [--headless] [--autopair [2|3]] [--lang sv|en]

- `--port`         TCP-port, standard 4242
- `--seats`        antal fasta platser, standard 10 (max 32)
- `--leaderboard`  fil där topplistan sparas, standard `battleship-leaderboard.txt` i aktuell katalog
- `--grace`        sekunder en spelare som tappat anslutningen får komma tillbaka innan matchen bryts, standard 90
- `--show-misses`  trekamp: visa även andras bommar från start (standard: bara träffar)
- `--paused`       starta pausad (samma som att trycka `P` direkt); `kill -USR1 <pid>` växlar paus utifrån
- `--headless`     ingen konsol, logg till stdout (används automatiskt när stdin inte är en tty)
- `--autopair`     para väntande platser automatiskt i platsordning, två eller tre åt gången (när ingen admin finns)
- `--lang`         `sv` (standard) eller `en`; kan också sättas med miljövariabeln `BATTLESHIP_LANG`

### Adminkonsolen

Servern visar en tabell över alla platser (klient-id, spelarens namn, status,
match, vinster/förluster denna session), pågående matcher och topplistan.
`Tab` växlar fokus mellan platstabellen och topplistan.

Fokus på platser:

| Tangent       | Åtgärd                                                   |
|---------------|----------------------------------------------------------|
| Upp/Ner, j/k  | flytta markören                                          |
| Mellanslag    | markera/avmarkera platsen under markören (max tre)       |
| p eller Enter | para de markerade platserna: två = vanlig match, tre = trekamp |
| u             | bryt matchen för platsen under markören                  |
| r             | starta om pågående match (tomma bräden, ingen poäng)     |
| m             | trekamp: visa/dölj andras bommar (standard dolda)        |
| P             | paus på/av: avslutade matcher väntar på nästa omgång och autoparning stannar; manuell parning fungerar fortfarande |
| e             | byt namn på spelaren vid platsen (skickas till klienten) |
| x             | sparka platsen under markören                            |
| Tab           | gå till topplistan                                       |
| q             | stäng servern                                            |

Fokus på topplistan:

| Tangent       | Åtgärd                                                   |
|---------------|----------------------------------------------------------|
| Upp/Ner, j/k  | flytta markören                                          |
| e eller Enter | byt namn på raden; finns namnet redan slås raderna ihop  |
| d             | ta bort raden                                            |
| R             | läs om topplistefilen från disk                          |
| Tab           | tillbaka till platserna                                  |

Endast platser med status `lobby` kan paras. **Ett par förblir ihop:** när en
match är slut startar nästa match mellan samma två spelare automatiskt
(förloraren är först), utom när servern är pausad (`P`): då ligger
resultatet kvar hos spelarna tills ledaren trycker `P` igen, och alla
väntande omgångar startar. Paret bryts bara när admin trycker `u`, eller när en av
spelarna kopplar ner. Då hamnar spelarna i lobbyn igen och kan paras om.

### Trekamp (tre spelare)

Paras tre platser startar en trekamp. Turen går runt A → B → C; varje skott
riktas mot valfri motståndare. På skärmen ligger de två fiendernas rutnät
sida vid sida överst, den egna flottan under. `Tab` byter mål, och siktet
hoppar också över till det andra rutnätet när man kör ut över kanten. Målets
rubrik är markerad. Varje fiendebräde är ett enda bräde: träffar som den andra
motståndaren gör syns också. Dennes bommar är dolda som standard (ledaren kan
visa dem med `m`). Du får alltid skjuta på en ruta där någon annan bommat;
det blir då en bom för dig också. Den vars flotta sänks är utslagen men ser resten av matchen;
sista flottan som flyter vinner (en vinst, två förluster i topplistan).
Lämnar en spelare en pågående trekamp spelar de två andra vidare, och nästa
omgång blir en vanlig match mellan dem.

### Tappad anslutning

Om en klient försvinner mitt i en match (kabel, laptop som somnar, fönster som
stängs) håller servern platsen i `--grace` sekunder. Klienten försöker själv
ansluta igen varannan sekund, och när den kommer tillbaka med samma klient-id
får den hela matchläget tillbaka (flotta, skott åt båda håll, vems tur) och
spelet fortsätter. Motståndaren ser "väntar på att hen kommer tillbaka" under
tiden. Kommer spelaren inte tillbaka i tid bryts matchen och motståndaren
hamnar i lobbyn. I konsolen visas en sådan plats som `borta 12s`.

Samma klient-id räknas alltid som samma spelare: ansluter en klient medan den
gamla anslutningen fortfarande ser levande ut (utdragen kabel ger ingen
signal) tar den nya över platsen, och den gamla får beskedet och slutar
försöka. Standard-id:t är datornamn plus terminal (t.ex. `pc-pts3`), så flera
klienter på samma dator får olika id; sätt annars ett unikt `--id`.

### Topplistan

Topplistan räknar vinster, förluster och träffsäkerhet (träffar/skott, alla
tider) per spelarnamn (skiftlägesokänsligt, även för åäö) och överlever omstarter: den skrivs till `--leaderboard`-filen
efter varje match och läses in när servern startar. Topp tio skickas till alla
klienter så att barnen ser den i lobbyn.

Under topplistan visas **Träffsäkerhet ikväll**: träffprocent sedan servern
startades, sorterad efter procent med antal `(träffar/skott)` så att små
underlag syns. Den uppdateras efter varje skott och nollställs när servern
startas om. Varje spelare ser dessutom sin egen träffprocent i pågående match
bredvid fiendens skeppsrad (`Träffar 5/12 (42%)`).

Filen är vanlig text med huvudet `# v2`, en rad per spelare:
`vinster<TAB>förluster<TAB>träffar<TAB>skott<TAB>namn`. Äldre filer utan
träffar/skott läses in och uppgraderas vid nästa sparning.
Den kan redigeras för hand med servern stoppad, eller med servern igång följt
av `R` i konsolen. Rader med samma namn slås ihop vid inläsning.

## Klient

    ./battleship client <värd> [--id ID] [--port N] [--seat N] [--lang sv|en] [--cell 1-3] [--logo] [--sound] [--flash]
    ./battleship client --test        # mot en robot
    ./battleship client --test3       # trekamp mot två robotar

- `värd`      serverns IPv4-adress (eller `localhost`)
- `--id`      klient-id som admin ser i konsolen, t.ex. `dator-3` (standard: datornamn+terminal; `A-Z a-z 0-9 _ -`, max 15). `--client` är samma sak.
- `--name`    förifyllt spelarnamn; utan den får spelaren skriva sitt namn vid start
- `--port`    serverport, standard 4242
- `--seat`    begär en viss plats; utan den får du första lediga
- `--lang`    `sv` (standard) eller `en`
- `--cell`    rutstorlek i rader (bredd = dubbla); utan den väljs största som får plats
- `--logo`    visa en stor SCOUT-banner under topplistan
- `--sound`   pip när det blir din tur
- `--flash`   blinka skärmen när det blir din tur
- `--test`    testläge: startar en lokal server och en robotmotståndare i bakgrunden så att
              hela flödet (namn, placering, strid, nästa match, topplista) kan provas på en
              enda dator. Topplistan i testläget är tillfällig och tas bort vid avslut.

Vid start frågar klienten **Vad heter du?** Spelaren skriver sitt namn (åäö och
mellanslag går bra, max 24 byte) och trycker Enter. Efter varje match, när man
trycker Enter för nästa, frågar klienten **Vem spelar nästa match?** med tomt
fält: den som ska spela måste skriva sitt namn innan nästa match startar. I
lobbyn och under placeringen byter `n` namn. Namnet är det som visas för
motståndaren och räknas i topplistan; klient-id:t är bara för admin.

Skärmen visar fiendens vatten överst och din egen flotta under, med topplistan
till höger om det finns plats (och SCOUT-bannern under den med `--logo`). Vatten ritas som `≈`, skepp som `█`, träff som
`╳` och bom som `○`. Med en terminal på minst 55 rader blir varje ruta 2x4
tecken (ser kvadratisk ut), annars 1x2. Minsta terminal: 36 rader x 25
kolumner; ca 90 kolumner rekommenderas. Kräver UTF-8-terminal.

### Tangenter

Tangenterna är skiftlägesokänsliga (Caps Lock spelar ingen roll), och
Mellanslag och Enter gör samma sak under spelet.

Placeringsfas (efter att admin parat dig):

| Tangent          | Åtgärd                                  |
|------------------|-----------------------------------------|
| Pilar / hjkl     | flytta skeppet                          |
| r                | rotera                                  |
| Mellanslag/Enter | placera skeppet                         |
| Backsteg         | ångra senast placerade skepp            |
| a                | auto-placera resterande skepp           |
| n                | byt namn                                |

Nästa skepp dyker upp på en ledig, giltig plats, och skeppet hålls alltid
innanför brädet när man flyttar eller roterar.

Stridsfas:

| Tangent             | Åtgärd                                  |
|---------------------|-----------------------------------------|
| Pilar / hjkl        | flytta siktet på fiendens rutnät        |
| Mellanslag / Enter  | skjut                                   |

Siktet är ett svart block på klargul botten när det är din tur och vitt på
mörkgrått medan motståndaren skjuter; statusraden är grön på din tur och
mörkgrå medan du väntar. Färgerna är fasta (xterm-palettens 256 färger) och
följer inte terminalens tema. Med `--sound`
och/eller `--flash` piper respektive blinkar klienten dessutom när turen blir
din (av som standard).

Felsökning: `BATTLESHIP_DEBUG=/tmp/bs.log ./battleship client ...` skriver alla
meddelanden från servern till filen.

Efter matchen ligger slutställningen kvar på skärmen; `Enter` går vidare via
namnfrågan till placering inför nästa match mot samma motståndare. Klienten
har ingen avsluta-tangent (barnen ska inte råka stänga den); ledaren avslutar
med `Ctrl+C`, som stänger ner snyggt.

Flotta: Hangarfartyg 5, Slagskepp 4, Kryssare 3, Ubåt 3, Jagare 2. Skepp får
inte röra varandra, inte ens diagonalt; det måste finnas minst en ruta vatten
emellan. Regeln gäller manuell placering, auto-placering och kontrolleras även
av servern. Turen växlar efter varje skott. Den som först sänker alla fem
skepp vinner.

## Projektorvy

    ./battleship board <värd> [--port N] [--logo] [--lang sv|en]

En läs-vy för en projektor eller stor skärm: topplistan, pågående matcher
(vem möter vem, vem skjuter, antal skott) och de senaste resultaten. Ansluter
om automatiskt om servern startas om. Avsluta med `Ctrl+C`.

## Protokoll

Ren text, ett radavslutat meddelande per rad, så det går att testa med `nc`.
Klient till server: `HELLO <plats|0> <klient-id>`, `NAME <namn>`,
`FLEET r,c,h;...`, `FIRE r c [målplats]`, `BYE`; projektorvyn skickar `WATCH <id>`.
Server till klient: `WELCOME`, `NAMEOK`, `OPPNAME`, `LOBBY`, `MATCH`/`MATCH3`,
`FLEETOK`, `START`, `TURN`, `SHOT`, `INCOMING`, `OSHOT`, `OUT`, `GAMEOVER`, `OPPLEFT`,
`ABORT`, `RESTART`, `LBCLEAR`, `LB <v> <f> <namn>`, `OPPWAIT`, `OPPBACK`,
`RESUME`/`RESUME3`/`MYFLEET`/`MYSHOTS`/`ENEMYSHOTS`/`ENEMYSUNK` (återupptagen match),
`MCLEAR`/`M`/`MEND`/`EV` (projektorvy), `ERR`.
Servern äger allt speltillstånd och validerar varje drag; en klient får aldrig
motståndarens skeppspositioner.
