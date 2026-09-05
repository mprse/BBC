# BBC — Bi-Bi-Coin, własna edukacyjna kryptowaluta

> Ten plik jest pierwotnym dokumentem koncepcyjnym i historycznym planem prac.
> Aktualny, przetestowany projekt, interfejsy i instrukcje opisuje angielska
> dokumentacja w katalogu `docs/`, rozpoczynająca się od `docs/README.md`.

## 1. Cel projektu

Celem projektu **BBC** jest stworzenie od podstaw prostej, działającej kryptowaluty i sieci blockchain w C++, przede wszystkim po to, aby praktycznie zrozumieć:

- czym jest portfel i para kluczy kryptograficznych,
- jak podpisywana jest transakcja,
- jak full node weryfikuje transakcje,
- czym jest mempool,
- jak transakcje trafiają do bloków,
- jak działa Proof of Work,
- po co miner szuka `nonce`,
- jak sieć radzi sobie z dwoma konkurencyjnymi historiami,
- jak działa fork i reorganizacja blockchaina,
- jak nody odnajdują się i komunikują w sieci P2P,
- dlaczego blockchain może działać bez centralnego serwera.

Projekt ma być **edukacyjny**, a nie produkcyjną kryptowalutą przeznaczoną do przechowywania realnej wartości.

---

# 2. Założenia ogólne

Pierwsza wersja będzie:

- napisana w **C++**,
- przenośna między **Linux / Windows / macOS**,
- uruchamiana głównie z **konsoli**,
- posiadała opcjonalny lokalny interfejs HTTP do podglądu stanu w przeglądarce,
- korzystała z komunikacji **TCP** pomiędzy nodami,
- używała modelu **kont**, podobnego koncepcyjnie do Ethereum,
- wykorzystywała **Proof of Work**,
- umożliwiała uruchomienie wielu nodów lokalnie na różnych portach.

Przykład:

```text
bbc --port 8001 --full-node --miner
bbc --port 8002 --full-node
bbc --port 8003 --wallet
```

Dzięki temu całą sieć będzie można początkowo testować na jednym komputerze.

---

# 3. Role w systemie

Jedna aplikacja `bbc` może pełnić kilka ról jednocześnie.

## 3.1 Wallet

Wallet odpowiada za:

- wygenerowanie klucza prywatnego,
- wyliczenie klucza publicznego,
- utworzenie adresu BBC,
- przechowywanie klucza prywatnego,
- tworzenie transakcji,
- podpisywanie transakcji,
- wysyłanie transakcji do full noda,
- wyświetlanie salda i historii użytkownika.

Wallet **nie musi posiadać całego blockchaina**.

```text
Wallet
  |
  | podpisana transakcja
  v
Full Node
```

## 3.2 Full Node

Full node jest jednym z najważniejszych elementów sieci.

Odpowiada za:

- przechowywanie blockchaina,
- przechowywanie aktualnego stanu kont,
- utrzymywanie mempoola,
- weryfikację transakcji,
- weryfikację bloków,
- przekazywanie transakcji innym nodom,
- przekazywanie bloków innym nodom,
- synchronizację blockchaina,
- obsługę forków i reorganizacji,
- niezależne egzekwowanie reguł protokołu.

Full node nie musi być minerem.

## 3.3 Miner

Miner:

- pobiera poprawne transakcje z mempoola,
- buduje kandydat na nowy blok,
- wykonuje Proof of Work,
- szuka wartości `nonce`, dla której hash nagłówka bloku spełnia wymagany target,
- po znalezieniu rozwiązania rozsyła blok do sieci,
- otrzymuje nagrodę za poprawnie zaakceptowany blok.

Miner może działać razem z full nodem:

```text
Wallet + Full Node + Miner
```

albo korzystać z osobnego full noda.

---

# 4. Tożsamość użytkownika

Nie istnieje centralna baza użytkowników ani serwer tworzący identyfikatory.

Przy tworzeniu portfela aplikacja lokalnie generuje parę kluczy:

```text
private key
    |
    v
public key
    |
    v
address BBC
```

## 4.1 Klucz prywatny

Klucz prywatny:

- jest losową tajną wartością,
- nigdy nie jest wysyłany do sieci,
- daje możliwość wydawania środków przypisanych do danego adresu,
- musi być chroniony przez właściciela.

Utrata klucza prywatnego bez backupu oznacza utratę możliwości dysponowania środkami.

Blockchain nadal może zawierać informację:

```text
BBC_ABC... = 10 000 BBC
```

ale bez klucza prywatnego nie będzie można utworzyć poprawnego podpisu transakcji.

## 4.2 Klucz publiczny

Klucz publiczny można udostępnić każdemu.

Służy przede wszystkim do **weryfikacji podpisów cyfrowych**.

Nie umożliwia praktycznego odtworzenia klucza prywatnego.

## 4.3 Adres

Adres konta będzie wyprowadzany z klucza publicznego, np. koncepcyjnie:

```text
address = HASH(public_key)
```

W rzeczywistej implementacji adres powinien zawierać także wersję/identyfikator sieci i sumę kontrolną.

Przykład:

```text
BBC_7F3A9C21...
```

Dzięki temu full node może sprawdzić, czy klucz publiczny podany przez nadawcę rzeczywiście odpowiada adresowi `from`.

---

# 5. Backup portfela

Blockchain można ponownie pobrać z sieci.

Klucza prywatnego nie można odzyskać z blockchaina.

Dlatego portfel powinien umożliwiać backup sekretu umożliwiającego odtworzenie kluczy.

Docelowo:

```text
recovery seed
    |
    v
private key
    |
    v
public key
    |
    v
address
```

Pierwsza implementacja może przechowywać zaszyfrowany plik:

```text
~/.bbc/wallet.dat
```

Klucz prywatny powinien być zaszyfrowany kluczem wyprowadzonym z hasła użytkownika za pomocą odpowiedniego KDF.

Nie zapisujemy klucza prywatnego jawnie w pliku tekstowym.

---

# 6. Kryptografia

## 6.1 Podpisy

Rekomendacja dla BBC:

**Ed25519**.

Jest to schemat podpisu wykorzystujący kryptografię krzywych eliptycznych i jest prosty do poprawnego użycia z gotową, sprawdzoną biblioteką.

Nie implementujemy algorytmu kryptograficznego samodzielnie.

W C++ możemy użyć np. sprawdzonej biblioteki kryptograficznej zapewniającej Ed25519.

Schemat:

```text
private_A
    |
    + transaction bytes
    |
    v
SIGN
    |
    v
signature
```

Do sieci trafiają:

```text
transaction
public_key_A
signature
```

Nigdy:

```text
private_key_A
```

## 6.2 Weryfikacja podpisu

Full node otrzymuje transakcję i wykonuje:

```text
VERIFY(public_key_A, transaction_bytes, signature)
```

Wynik:

```text
true  -> podpis kryptograficznie poprawny
false -> transakcję odrzucamy
```

Podpis wiąże konkretną treść transakcji z kluczem prywatnym właściciela.

Jeśli ktoś zmieni:

```text
A -> B : 10 BBC
```

na:

```text
A -> C : 100 BBC
```

podpis przestanie być poprawny.

## 6.3 Próba podszycia się pod innego użytkownika

Załóżmy:

```text
A:
  private_A
  public_A
  address_A = HASH(public_A)

C:
  private_C
  public_C
  address_C = HASH(public_C)
```

C próbuje utworzyć:

```text
from       = address_A
to         = address_B
amount     = 10
public_key = public_C
signature  = SIGN(private_C, transaction)
```

Sam podpis względem `public_C` będzie poprawny.

Dlatego node wykonuje dwie osobne kontrole:

```text
1. address(public_key_C) == from ?
2. VERIFY(public_key_C, transaction, signature) ?
```

Pierwsza kontrola da:

```text
address_C != address_A
```

więc transakcja zostanie odrzucona.

---

# 7. Hashowanie

Hashowanie jest osobnym mechanizmem od podpisu cyfrowego.

W projekcie możemy używać **SHA-256** do:

- identyfikowania transakcji,
- identyfikowania bloków,
- powiązania bloków w łańcuch,
- Proof of Work,
- budowy struktur typu Merkle tree w późniejszej wersji.

Hash ma właściwość lawinową:

minimalna zmiana danych powoduje całkowicie inny wynik.

```text
HASH("A -> B : 10") = X
HASH("A -> B : 11") = zupełnie inne Y
```

---

# 8. Model kont

BBC używa modelu kont.

Full node utrzymuje aktualny stan:

```text
address_A -> balance: 100 BBC, nonce: 7
address_B -> balance:  20 BBC, nonce: 3
address_C -> balance:   0 BBC, nonce: 0
```

Blockchain pozostaje historią pozwalającą dojść do tego stanu.

Aktualna baza stanu umożliwia szybkie sprawdzanie sald bez analizowania całego blockchaina przy każdej transakcji.

---

# 9. Format transakcji

Pierwsza wersja może mieć logicznie następujący format:

```text
Transaction {
    version
    chain_id
    sender_public_key
    recipient_address
    amount
    fee
    nonce
    signature
}
```

`from` może być wyliczany z `sender_public_key`, dzięki czemu nie trzeba przechowywać dwóch potencjalnie sprzecznych pól.

## 9.1 Ważna zasada: żadnych floatów

Kwoty przechowujemy jako liczby całkowite najmniejszej jednostki.

Przykład:

```text
1 BBC = 100 000 000 bbc-unit
```

Czyli:

```text
10 BBC = 1 000 000 000 bbc-unit
```

Nigdy:

```cpp
double amount;
```

Tylko np.:

```cpp
uint64_t amount;
```

## 9.2 Nonce transakcji

To **inne nonce** niż nonce używane przez minera w Proof of Work.

Nonce konta zapobiega ponownemu użyciu tej samej transakcji i określa kolejność transakcji nadawcy.

Jeśli stan A zawiera:

```text
nonce_A = 7
```

następna zaakceptowana transakcja A musi np. mieć:

```text
nonce = 8
```

Po jej wykonaniu:

```text
nonce_A = 8
```

Ponowne wysłanie tej samej podpisanej transakcji zostanie odrzucone.

---

# 10. Tworzenie transakcji krok po kroku

A chce wysłać B 10 BBC.

## Krok 1 — wallet tworzy dane

```text
recipient = address_B
amount    = 10 BBC
fee       = 0.01 BBC
nonce     = następny nonce konta A
chain_id  = BBC_MAINNET
```

## Krok 2 — kanoniczna serializacja

Każdy node musi otrzymać dokładnie ten sam ciąg bajtów do podpisania.

Nie podpisujemy przypadkowo sformatowanego JSON-a.

Potrzebujemy jasno zdefiniowanej binarnej serializacji.

## Krok 3 — podpis

```text
signature = SIGN(private_A, serialized_transaction_without_signature)
```

## Krok 4 — wysłanie

Wallet przekazuje transakcję do znanego full noda.

```text
Wallet A
   |
   v
Full Node 1
```

---

# 11. Weryfikacja transakcji przez full node

Node po otrzymaniu transakcji sprawdza między innymi:

1. czy format jest poprawny,
2. czy wersja protokołu jest obsługiwana,
3. czy `chain_id` jest poprawny,
4. czy adres nadawcy odpowiada jego kluczowi publicznemu,
5. czy podpis jest poprawny,
6. czy kwota jest większa od zera,
7. czy nie wystąpi przepełnienie integera,
8. czy nadawca ma wystarczające środki,
9. czy nonce transakcji jest prawidłowy,
10. czy transakcja nie została już przetworzona,
11. czy fee spełnia reguły protokołu.

Jeśli wszystko jest poprawne:

```text
mempool.add(transaction)
```

Następnie node informuje swoich peerów o nowej transakcji.

---

# 12. Mempool

Mempool jest lokalnym zbiorem poprawnych, ale jeszcze niezatwierdzonych w blockchainie transakcji.

```text
mempool:

TX1 A -> B : 10 BBC
TX2 D -> E :  5 BBC
TX3 F -> G :  2 BBC
```

Każdy full node może mieć przez chwilę nieco inny mempool ze względu na opóźnienia sieciowe.

To jest normalne.

---

# 13. Blok

Logiczna struktura bloku:

```text
Block {
    header
    transactions[]
}
```

Nagłówek może zawierać:

```text
BlockHeader {
    version
    previous_block_hash
    transaction_root
    timestamp
    difficulty_target
    nonce
}
```

`previous_block_hash` łączy blok z poprzednim blokiem.

```text
Block 100
hash = AAA
    |
    v
Block 101
previous_hash = AAA
hash = BBB
    |
    v
Block 102
previous_hash = BBB
```

Zmiana starego bloku zmienia jego hash i przerywa powiązanie z następnym blokiem.

---

# 14. Proof of Work

Proof of Work nie służy przede wszystkim do sprawdzania podpisów transakcji.

Podpisy i reguły full noda odpowiadają za pytanie:

> Czy transakcja jest poprawna?

Proof of Work odpowiada przede wszystkim za inne pytanie:

> Która z konkurencyjnych poprawnych historii ma być uznana przez sieć?

## 14.1 Target

Sieć ustala warunek:

```text
block_hash < target
```

Dla łatwego testu można sobie wyobrazić warunek:

```text
hash zaczyna się od 0000
```

W rzeczywistości porównujemy liczbę reprezentowaną przez hash z wartością target.

## 14.2 Nonce minera

Miner posiada kandydat na blok i zmienia `nonce`:

```text
nonce = 0 -> HASH(header) -> nie pasuje
nonce = 1 -> HASH(header) -> nie pasuje
nonce = 2 -> HASH(header) -> nie pasuje
...
nonce = N -> HASH(header) < target -> sukces
```

Nonce nie ma specjalnego znaczenia biznesowego.

Jest po prostu polem, które miner może zmieniać, aby otrzymywać kolejne praktycznie losowe wartości hash.

## 14.3 Dlaczego to kosztuje pracę

Nie istnieje praktyczny sposób przewidzenia wartości nonce dającej odpowiednio mały hash.

Trzeba wykonywać wiele prób.

```text
znalezienie -> kosztowne
sprawdzenie -> bardzo tanie
```

Full node po otrzymaniu bloku wykonuje jedno hashowanie i szybko sprawdza:

```text
HASH(header) < target ?
```

---

# 15. Dlaczego Proof of Work zabezpiecza historię

Załóżmy, że A ma 10 BBC i wysyła dwie poprawnie podpisane transakcje:

```text
TX1: A -> B : 10 BBC
TX2: A -> C : 10 BBC
```

Obie zostały podpisane przez A.

Problem nie polega więc na sfałszowanym podpisie.

Problem brzmi:

> Która z dwóch transakcji stanie się częścią obowiązującej historii?

Dwie grupy minerów mogą chwilowo utworzyć dwa bloki:

```text
             Block 100
             /       \
            /         \
      Block 101A     Block 101B
      A -> B          A -> C
```

Oba bloki mogą być lokalnie poprawne względem stanu z Block 100.

Sieć ma chwilowy fork.

---

# 16. Fork i wybór historii

Każdy full node samodzielnie stosuje tę samą regułę:

> Głównym łańcuchem jest poprawny łańcuch o największej skumulowanej pracy Proof of Work.

Nie istnieje centralny arbiter.

Przez chwilę może istnieć remis:

```text
             100
           /     \
        101A     101B
          |        |
        102A     102B
```

Różne nody mogą chwilowo uznawać różne końcówki za aktualne.

Gdy jedna gałąź zgromadzi większą pracę:

```text
             100
           /     \
        101A     101B
          |        |
        102A     102B
          |
        103A
```

nody przełączają się na gałąź A.

To nazywa się **reorganizacją blockchaina (reorg)**.

---

# 17. Co dzieje się z transakcjami przegranego forka

Załóżmy, że przegrana gałąź zawierała:

```text
101B: A -> C : 10 BBC
102B: D -> E :  5 BBC
103B: F -> G :  2 BBC
```

Po reorgu node ponownie ocenia transakcje.

Jeśli:

```text
D -> E
F -> G
```

są nadal poprawne względem zwycięskiego łańcucha, mogą wrócić do mempoola.

Jeśli natomiast zwycięski łańcuch zawiera już:

```text
A -> B : 10 BBC
```

i A nie ma kolejnych środków, to:

```text
A -> C : 10 BBC
```

jest już nieważne.

---

# 18. Potwierdzenia

Transakcja znajdująca się w najnowszym bloku nie ma absolutnej natychmiastowej finalności.

Może wystąpić krótki fork i reorg.

Im więcej bloków zostanie zbudowanych nad blokiem zawierającym transakcję, tym trudniej zastąpić tę historię alternatywnym łańcuchem z większą pracą.

```text
TX w Block 100
       |
      101
       |
      102
       |
      103
```

Kolejne bloki zwiększają praktyczną pewność transakcji.

---

# 19. Nagroda minera

Blok zawiera specjalną transakcję tworzącą nagrodę dla minera.

Przykład:

```text
block reward = 50 BBC
+ transaction fees
```

To jedna z metod wprowadzania nowych BBC do obiegu.

Reguła emisji musi być częścią protokołu i każdy full node musi ją weryfikować.

Miner nie może sam wpisać sobie dowolnej liczby BBC.

Jeżeli nagroda przekracza dozwoloną wartość:

```text
INVALID BLOCK
```

---

# 20. Genesis Block

Sieć musi posiadać pierwszy blok — **Genesis Block**.

Jego zawartość i hash są częścią konfiguracji protokołu.

Może np. definiować początkowe parametry sieci.

Do decyzji pozostaje, czy:

1. Genesis nie przydziela nikomu monet, a wszystkie BBC powstają przez mining,
2. Genesis tworzy początkową pulę BBC,
3. stosujemy model mieszany.

Dla projektu edukacyjnego interesujący jest wariant 1.

---

# 21. Sieć P2P

Nie chcemy centralnego serwera przekazującego wszystkie transakcje.

Nody komunikują się bezpośrednio:

```text
        Node B
       /      \
Node A          Node D
       \      /
        Node C
```

Każdy node utrzymuje połączenia z kilkoma peerami.

## 21.1 Dlaczego TCP

Pierwsza wersja używa **TCP**, ponieważ potrzebujemy niezawodnego przesyłania:

- transakcji,
- bloków,
- fragmentów blockchaina,
- list peerów.

TCP upraszcza:

- retransmisję,
- kolejność danych,
- obsługę utraconych pakietów.

UDP nie jest potrzebne w pierwszej wersji.

---

# 22. Odkrywanie peerów

Nowy node musi wiedzieć, z kim połączyć się po pierwszym uruchomieniu.

Możemy posiadać kilka adresów bootstrap/seed:

```text
seed1.bbc.example
seed2.bbc.example
```

Seed nie przechowuje blockchaina w imieniu użytkowników i nie przekazuje wszystkich transakcji.

Służy tylko do uzyskania pierwszych adresów peerów.

```text
New Node
   |
   v
Seed
   |
   v
Node A, Node B, Node C
```

Potem node pyta peerów o kolejnych peerów.

Jeśli seed zostanie wyłączony, istniejąca połączona sieć powinna nadal działać.

W przyszłości można dodać bardziej zdecentralizowane mechanizmy discovery.

---

# 23. Propagacja transakcji

A wysyła transakcję do jednego lub kilku znanych nodów:

```text
Wallet A
   |
   v
Node 1
 /   \
Node2 Node3
 |     |
Node4 Node5
```

Node nie musi zawsze przesyłać pełnej transakcji wszystkim natychmiast.

W późniejszej wersji można najpierw rozgłaszać identyfikator transakcji i pobierać dane tylko wtedy, gdy peer jej jeszcze nie zna.

Dla pierwszej wersji prostsze będzie bezpośrednie przesyłanie małych transakcji.

---

# 24. Propagacja bloków

Miner po znalezieniu Proof of Work wysyła nowy blok do swoich peerów.

Każdy full node:

1. odbiera blok,
2. sprawdza format,
3. sprawdza `previous_block_hash`,
4. sprawdza Proof of Work,
5. sprawdza wszystkie transakcje,
6. sprawdza poprawność nagrody minera,
7. sprawdza wynikowy stan kont,
8. zapisuje poprawny blok,
9. przekazuje go dalej.

---

# 25. Przechowywanie danych

Przykładowy katalog noda:

```text
~/.bbc/
    config.toml
    wallet.dat
    peers.dat

    chain/
        blocks.dat
        index.db
        state.db
```

## 25.1 blocks.dat

Zawiera trwałą historię bloków.

## 25.2 state.db

Zawiera aktualny stan kont:

```text
address -> balance, nonce
```

Może zostać odbudowany poprzez ponowne przetworzenie blockchaina.

## 25.3 peers.dat

Zapamiętane adresy innych nodów.

## 25.4 wallet.dat

Zawiera dane portfela i wymaga szczególnej ochrony.

---

# 26. Synchronizacja nowego full noda

Nowy full node nie ufa pojedynczemu serwerowi.

Łączy się z peerami i pobiera nagłówki/bloki.

Pierwsza, prosta wersja może działać tak:

```text
1. connect peer
2. GET_CHAIN_TIP
3. porównaj height/work
4. GET_BLOCKS od znanego punktu
5. weryfikuj każdy blok lokalnie
6. zbuduj state.db
```

Node nie powinien przyjmować informacji typu:

```text
"A ma 100 BBC, zaufaj mi"
```

Powinien sam dojść do stanu na podstawie zweryfikowanego blockchaina.

---

# 27. Podstawowe wiadomości protokołu P2P

Pierwsza wersja może zawierać np.:

```text
HELLO
PEERS
GET_PEERS
NEW_TRANSACTION
NEW_BLOCK
GET_BLOCK
BLOCK
GET_HEADERS
HEADERS
PING
PONG
```

Każda wiadomość powinna mieć jednoznaczny nagłówek, np.:

```text
magic
protocol_version
message_type
payload_size
payload_checksum
payload
```

`magic` pozwala odróżnić BBC od przypadkowych danych lub innej sieci.

---

# 28. Lokalny interfejs WWW

Podstawowy program pozostaje aplikacją konsolową.

Opcjonalnie node może uruchamiać lokalny serwer HTTP:

```text
http://127.0.0.1:8080
```

Panel może pokazywać:

- adres portfela,
- saldo,
- wysokość blockchaina,
- hash ostatniego bloku,
- liczbę peerów,
- zawartość mempoola,
- ostatnie bloki,
- ostatnie transakcje,
- status minera,
- hashrate,
- aktualny target/difficulty.

Na początku API powinno domyślnie nasłuchiwać tylko na `localhost`.

---

# 29. Proponowana struktura kodu C++

```text
bbc/
├── CMakeLists.txt
├── README.md
├── docs/
│   └── protocol.md
│
├── src/
│   ├── main.cpp
│   │
│   ├── crypto/
│   │   ├── keys.cpp
│   │   ├── signature.cpp
│   │   └── hash.cpp
│   │
│   ├── wallet/
│   │   ├── wallet.cpp
│   │   └── address.cpp
│   │
│   ├── chain/
│   │   ├── transaction.cpp
│   │   ├── block.cpp
│   │   ├── blockchain.cpp
│   │   ├── state.cpp
│   │   └── validation.cpp
│   │
│   ├── mempool/
│   │   └── mempool.cpp
│   │
│   ├── consensus/
│   │   ├── pow.cpp
│   │   ├── difficulty.cpp
│   │   └── fork_choice.cpp
│   │
│   ├── network/
│   │   ├── peer.cpp
│   │   ├── protocol.cpp
│   │   ├── server.cpp
│   │   └── discovery.cpp
│   │
│   ├── storage/
│   │   ├── block_store.cpp
│   │   └── state_store.cpp
│   │
│   └── rpc/
│       └── http_server.cpp
│
└── tests/
```

Podział może ewoluować podczas implementacji.

---

# 30. Proponowane komendy CLI

## Wallet

```text
bbc wallet create
bbc wallet restore
bbc wallet address
bbc wallet balance
bbc wallet backup
```

## Transakcje

```text
bbc send <address> <amount>
bbc tx get <txid>
```

## Node

```text
bbc node start
bbc node status
bbc node peers
bbc node mempool
```

## Mining

```text
bbc miner start
bbc miner stop
bbc miner status
```

## Blockchain

```text
bbc chain tip
bbc chain block <height/hash>
bbc chain verify
```

---

# 31. Pierwsza wersja MVP

Nie próbujemy od razu implementować całego systemu.

## Etap 1 — kryptografia i wallet

- generowanie Ed25519 keypair,
- tworzenie adresu,
- zapis/odczyt walleta,
- podpisywanie danych,
- weryfikowanie podpisu.

Test:

```text
create wallet A
create wallet B
sign message by A
verify with public_A
modify message
verify must fail
```

## Etap 2 — Transaction

- struktura `Transaction`,
- binarna kanoniczna serializacja,
- txid,
- podpis,
- walidacja podpisu,
- nonce konta,
- integer amount.

## Etap 3 — Block

- struktura bloku,
- previous hash,
- lista transakcji,
- hash bloku,
- Genesis Block.

## Etap 4 — Proof of Work

Na początku bardzo mała trudność:

```text
hash < test_target
```

Tak, aby na laptopie blok pojawiał się np. co kilka–kilkadziesiąt sekund podczas testów.

## Etap 5 — Blockchain + state

- dodawanie bloków,
- odrzucanie niepoprawnych bloków,
- aktualizacja sald,
- aktualizacja nonce kont,
- reward minera.

## Etap 6 — Mempool

- przyjmowanie transakcji,
- walidacja,
- wybieranie transakcji do bloku,
- usuwanie zatwierdzonych transakcji.

## Etap 7 — lokalne P2P

Uruchamiamy trzy procesy:

```text
Node A : 127.0.0.1:8001
Node B : 127.0.0.1:8002
Miner C: 127.0.0.1:8003
```

Testujemy:

```text
A -> B : 10 BBC
```

oraz propagację:

```text
A -> Node1 -> Node2 -> Miner
```

## Etap 8 — forki i reorg

To będzie jeden z najważniejszych testów projektu.

Celowo tworzymy:

```text
          Block N
          /     \
       N+1A     N+1B
```

Następnie dokładamy pracę do jednej gałęzi i sprawdzamy, czy drugi node wykonuje poprawny reorg.

## Etap 9 — Internet

- seed node,
- publiczne adresy,
- NAT/firewall considerations,
- połączenia wielu komputerów,
- synchronizacja od zera.

## Etap 10 — lokalny explorer WWW

Wyświetlenie:

- bloków,
- txid,
- kont,
- sald,
- mempoola,
- peerów,
- forka/reorgu.

---

# 32. Kluczowe scenariusze testowe

## Test 1 — poprawny przelew

```text
A = 100 BBC
B = 0 BBC

A -> B = 10 BBC
```

Po zatwierdzeniu:

```text
A = 90 BBC - fee
B = 10 BBC
```

## Test 2 — fałszywy podpis

C próbuje wysłać środki A podpisując transakcję `private_C`.

Oczekiwany wynik:

```text
INVALID TRANSACTION
```

## Test 3 — zmiana podpisanej transakcji

A podpisuje:

```text
A -> B : 10
```

Atakujący zmienia amount na 100.

Oczekiwany wynik:

```text
signature verification failed
```

## Test 4 — brak środków

```text
A balance = 10
A -> B = 20
```

Oczekiwany wynik:

```text
insufficient balance
```

## Test 5 — replay

Ta sama podpisana transakcja zostaje wysłana drugi raz.

Oczekiwany wynik:

```text
invalid account nonce
```

## Test 6 — double spend + fork

A ma 10 BBC.

```text
TX1: A -> B = 10
TX2: A -> C = 10
```

Dwie części sieci tworzą konkurencyjne bloki.

Oczekiwany wynik:

- chwilowy fork,
- jedna gałąź zdobywa większą cumulative work,
- reorg,
- tylko jeden z dwóch konfliktujących przelewów pozostaje w canonical chain.

## Test 7 — fałszywy blok minera

Miner próbuje dać sobie większą nagrodę niż przewiduje protokół.

Oczekiwany wynik:

```text
INVALID BLOCK
```

## Test 8 — niepoprawny PoW

Blok ma hash większy od target.

Oczekiwany wynik:

```text
INVALID PROOF OF WORK
```

---

# 33. Czego Proof of Work NIE robi

Proof of Work nie sprawdza, czy użytkownik zna klucz prywatny.

Nie zapobiega również sam w sobie stworzeniu dwóch poprawnie podpisanych, konfliktujących transakcji.

Rozdział odpowiedzialności jest następujący:

```text
Podpis cyfrowy
    -> czy właściciel klucza zatwierdził transakcję?

Stan konta + nonce
    -> czy transakcja może zostać wykonana w tym miejscu historii?

Full Node
    -> czy transakcja/blok spełnia wszystkie reguły protokołu?

Proof of Work
    -> jaki jest koszt stworzenia bloków i która konkurencyjna historia ma większą skumulowaną pracę?
```

To rozróżnienie jest kluczowe dla zrozumienia projektu.

---

# 34. Dwa różne nonce

W projekcie występują dwa niezależne pojęcia o tej samej nazwie.

## Transaction nonce

```text
A nonce = 17
```

Służy do:

- kolejności transakcji konta,
- ochrony przed replay,
- wykrywania konfliktujących operacji.

## Mining nonce

```text
block.nonce = 927361829
```

Służy tylko do zmieniania nagłówka podczas poszukiwania poprawnego Proof of Work.

Nie wolno ich ze sobą mylić.

---

# 35. Przykład całego przepływu A -> B

Założenia:

```text
A = zwykły wallet
B = wallet + full node
C = full node + miner

A balance = 100 BBC
B balance = 0 BBC
```

A chce wysłać B 10 BBC.

```text
1. A tworzy transaction

   recipient = B
   amount = 10 BBC
   nonce = N

2. A podpisuje transaction private_A

3. A wysyła transaction do znanego full noda

4. Full node sprawdza:
   - sender/public key
   - signature
   - balance
   - nonce
   - format

5. Poprawna TX trafia do mempoola

6. Full node rozsyła TX do peerów

7. Miner C otrzymuje TX

8. Miner buduje candidate block

9. Miner szuka mining nonce

10. HASH(block_header) < target

11. Miner rozsyła nowy blok

12. Full nody samodzielnie weryfikują blok

13. Poprawny blok zostaje częścią canonical chain

14. Stan zmienia się:

    A = 90 BBC - fee
    B = 10 BBC

15. Kolejne bloki zwiększają liczbę potwierdzeń TX
```

---

# 36. Co jest publiczne

W podstawowej wersji BBC blockchain nie zapewnia prywatności transakcji.

Publiczne są m.in.:

- adresy,
- kwoty,
- transakcje,
- bloki,
- podpisy,
- klucze publiczne użyte w transakcjach.

Tajny pozostaje:

```text
private key
```

Adres oznacza pseudonim, a nie automatycznie prawdziwą tożsamość człowieka.

---

# 37. Bez centralnego serwera

Serwer WWW projektu może istnieć jako wygodne narzędzie:

```text
bbc.example
├── strona projektu
├── seed service
├── explorer
└── dokumentacja
```

Ale nie powinien być wymagany do:

- zatwierdzania transakcji,
- przechowywania jedynej kopii blockchaina,
- przydzielania sald,
- wybierania zwycięskiego bloku,
- podpisywania transakcji użytkowników.

Jeśli serwer WWW przestanie działać, połączone full nody powinny nadal utrzymywać BBC.

---

# 38. Decyzje pozostające do ustalenia

Przed implementacją protokołu trzeba ustalić konkretne stałe:

1. Nazwa waluty — `Bi-Bi-Coin` (`BBC`).
2. Liczba miejsc dziesiętnych.
3. Initial block reward.
4. Czy reward zmniejsza się w czasie.
5. Target block time, np. 30 s dla sieci eksperymentalnej.
6. Algorytm dostosowania difficulty.
7. Maksymalny rozmiar bloku.
8. Maksymalny rozmiar transakcji.
9. Minimalne fee.
10. Maksymalna podaż lub brak sztywnego limitu.
11. Dokładny format adresu.
12. Dokładny format binarnego protokołu.
13. Format trwałego przechowywania blockchaina.
14. Biblioteki C++ do kryptografii, sieci i storage.

---

# 39. Najważniejsza zasada architektoniczna

Żaden node nie jest „serwerem prawdy”.

Każdy full node powinien być w stanie samodzielnie odpowiedzieć:

```text
Czy ta transakcja jest poprawna?
Czy ten blok jest poprawny?
Czy ten Proof of Work jest poprawny?
Który znany mi łańcuch ma największą skumulowaną pracę?
Jaki jest aktualny stan kont po wykonaniu canonical chain?
```

Jeśli wszystkie poprawnie zaimplementowane nody stosują identyczne reguły, sieć może dojść do wspólnej historii bez centralnego administratora.

---

# 40. Pierwszy milestone

Pierwszym realnym milestone'em projektu powinien być program działający **bez sieci**:

```text
./bbc-demo
```

który potrafi:

1. stworzyć portfele A i B,
2. przydzielić A środki w genesis,
3. utworzyć `A -> B : 10 BBC`,
4. podpisać ją private key A,
5. zweryfikować public key A,
6. odrzucić próbę podpisania jej przez C,
7. dodać transakcję do bloku,
8. znaleźć prosty Proof of Work,
9. zweryfikować blok,
10. zaktualizować saldo A i B.

Dopiero gdy to działa deterministycznie i posiada testy jednostkowe, dokładamy komunikację P2P.

To pozwoli oddzielić błędy blockchaina i kryptografii od błędów sieciowych.

---

# 41. Docelowa wizja pierwszej działającej sieci

```text
                         Internet / LAN

              +----------------------------+
              |                            |
              v                            v
        +------------+               +------------+
        | Full Node 1|<------------->| Full Node 2|
        | blockchain |               | blockchain |
        | mempool    |               | mempool    |
        +-----+------+               +------+-----+
              ^                             ^
              |                             |
              |                             |
      +-------+------+              +-------+------+
      | Wallet A     |              | Miner C      |
      | private key A|              | Full Node    |
      +--------------+              | Proof of Work|
                                    +--------------+

A tworzy i podpisuje TX.
Full nody ją weryfikują i propagują.
Miner tworzy blok i wykonuje PoW.
Full nody niezależnie weryfikują blok.
W przypadku forka wybierają chain o największej cumulative work.
```

---

# 42. Podsumowanie

BBC będzie małą, ale rzeczywiście rozproszoną kryptowalutą edukacyjną.

Najważniejsze elementy systemu:

```text
Wallet
  -> private/public key
  -> address
  -> podpisywanie transakcji

Full Node
  -> blockchain
  -> state
  -> mempool
  -> walidacja
  -> P2P

Miner
  -> candidate block
  -> Proof of Work
  -> mining nonce
  -> block reward

Consensus
  -> valid blocks only
  -> greatest cumulative Proof of Work
  -> forks
  -> reorgs
```

Najważniejsza intuicja całego projektu:

> **Podpis kryptograficzny mówi, kto autoryzował transakcję. Full node mówi, czy transakcja jest zgodna z regułami. Proof of Work pomaga rozproszonej sieci uzgodnić jedną historię spośród konkurencyjnych poprawnych historii.**
