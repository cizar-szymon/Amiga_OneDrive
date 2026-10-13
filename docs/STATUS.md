# AmiDrive – stan prac i decyzje

## Stan

| Krok | Stan |
|---|---|
| Środowisko: vbcc + NDK 3.2 + netinclude Roadshow + SDK AmiSSL 5.24 (`C:\amiga-dev\amissl`) | ✅ |
| A1200 (68060, OS 3.2, Roadshow 1.14, AmiSSL 5.24): zegar ustawiony, `TZ` poprawione | ✅ 13.10.2026 |
| Szkielet: `Makefile` (vbcc), `Makefile.host` (testy na PC), `tools/amiga_remote.py` | ✅ |
| `net_amiga.c`: gniazda + TLS z weryfikacją certyfikatu, nazwy hosta, SNI, min. TLS 1.2 | ✅ test na A1200 |
| `http.c`: HTTP/1.1, keep-alive, Content-Length / chunked / do zamknięcia, Location, Retry-After | ✅ testy + A1200 |
| `json.c`: jsmn + klucze, ścieżki, tablice, `\uXXXX` → UTF-8, liczby | ✅ testy |
| `fsmap.c`: UTF-8 ↔ Latin-1 / Latin-2, nazwy lokalne (skracanie z hashem), sprawdzanie nazw do wysłania | ✅ testy |
| `tz.c`: ISO 8601 ↔ sekundy od 1978, strefa POSIX `TZ` z czasem letnim | ✅ testy |
| `AmiDrive TEST` (zegar, strefa, DNS, TLS, 2 zapytania na jednym połączeniu) | ✅ A1200 |
| `session.c`: trwałe połączenie na host, ponowienie na nowym połączeniu, gdy serwer zamknął bezczynne | ✅ testy + A1200 |
| `config.c`: `AmiDrive.prefs` (`KLUCZ=wartość`), argumenty zapisywane | ✅ testy + A1200 |
| `plat_amiga.c`: czas UTC, czekanie z Ctrl-C, bezpieczny zapis plików (`.new` + rename) | ✅ A1200 |
| Rejestracja aplikacji w Entra (dzierżawa `cslabsc`, Azure CLI): client ID `0474e444-3266-446b-b49c-d47a01cadf56`, konta prywatne i firmowe, public client, `Files.ReadWrite` + `offline_access` | ✅ 14.10.2026 |
| `auth.c`: device code flow, odświeżanie (rotacja refresh tokenu), zapis w `ENVARC:AmiDrive/token` | ✅ testy + A1200 z prawdziwym kontem |
| `LOGIN`, `LOGOUT`, `INFO` (właściciel i zajętość OneDrive) | ✅ A1200, konto firmowe (OneDrive for Business) |
| `graph.c`: `/me/drive` | ✅ testy |
| `graph.c`: element po ścieżce, delta i children stronami (`nextLink`/`deltaLink`), pobieranie strumieniowe (302 → serwer plików), ponowienia 429/5xx, 404/410 | ✅ testy + A1200 (atrapy), ⏳ prawdziwe konto |
| Start z ikony (Tool Types, `WINDOW`, dziennik `.AmiDrive/AmiDrive.log`) | ⏳ |
| `state.c`: baza stanu po `id` (ścieżki z łańcucha rodziców), plik tekstowy zapisywany strumieniowo do `.new` i podmieniany, linia końcowa | ✅ testy + A1200 (`AmiDrive TEST` zapisuje i czyta stan w `T:`) |
| `plat`: strumieniowe pliki (`plat_open_read/write`, `plat_close` z podmianą) | ✅ A1200 |
| `sync.c`: etap 1 – pobieranie zmian (foldery, pobieranie, przenoszenie, usuwanie, ochrona plików zmienionych na Amidze, pełna lista po 410) | ✅ testy + A1200 z kontem Business (nowe pliki, usuwanie, zmiana nazwy, podfolder); ⏳ przeniesienie, długie nazwy, zagnieżdżenie |
| `plat`: informacja o pliku, data, zmiana nazwy, tworzenie wprost, limit nazw woluminu | ✅ A1200 (`AmiDrive TEST`) |
| `AmiDrive` / `AmiDrive ONCE`: synchronizacja co `INTERVAL` min albo raz, Ctrl-C, podsumowanie | ✅ `ONCE` na A1200; ⏳ praca w tle |
| Etap 2: wysyłanie, sesje wysyłania, konflikty | ⏳ |

## Wyniki testów

- PC (`make -f Makefile.host test`, gcc `-Wall -Wextra -pedantic`): 1193 sprawdzeń, 0 błędów.
- A1200 (`AmiDriveTests`, ten sam zestaw skompilowany vbcc): 1193 sprawdzeń, 0 błędów.
- A1200 `LOGIN` z prawdziwym identyfikatorem (14.10.2026): kod na `https://login.microsoft.com/device`,
  logowanie na telefonie/PC, token zapisany; `INFO`: konto firmowe, OneDrive for Business.
  Błąd znaleziony przy tym: `device_code` od Microsoftu ma 1045 znaków, bufor miał 1024
  („incomplete device code answer”) – teraz `AUTH_TOKEN_SIZE` (4096), test z kodem 1500 znaków.
- A1200, pierwsza prawdziwa synchronizacja (14.10.2026, OneDrive for Business,
  `REMOTE=/Retro/AmigaDrive`, `LOCAL=Work:OneDrive`): pusty folder → stan z deltaLinkiem;
  po wgraniu plików przejście przyrostowe pobrało 5 plików (3,6 MB, m.in. ADF-y 880 KB
  i 1,76 MB) z datami z chmury; następne przejście: „no changes”. Nazwa z `ł` przy
  `CHARSET=LATIN1` → `Bry_a1~77bc45.3mf` (hash, zgodnie z zasadą).
  Po `CHARSET=LATIN2` jedno pełne przejście zmieniło nazwę na `Bryła1.3mf` bez pobierania.
  Dalej (14.10.2026): usunięcie 5 plików w chmurze → usunięte lokalnie; 2 nowe pliki pobrane;
  zmiana nazwy pliku → zmiana nazwy lokalnie bez pobierania; nowy podfolder utworzony; kopia
  pliku („Kopiuj do”) = nowy element, pobrany. Nie sprawdzone jeszcze: przeniesienie wewnątrz
  folderu, nazwa > 30 znaków, folder w folderze, przeniesienie poza folder.
- A1200 `AmiDrive TEST`: TLS 1.3 `TLS_AES_256_GCM_SHA384`, nawiązanie połączenia z
  `graph.microsoft.com` ok. 2,3 s, drugie zapytanie na tym samym połączeniu 80 ms.
- A1200 `AmiDrive TEST HOST=...` (badssl.com): odrzucone `expired`, `wrong.host`, `self-signed`,
  `untrusted-root`; przyjęty `badssl.com`.

## Decyzje

- **Tryb pracy (użytkownik, 13.10.2026):** domyślnie praca w tle, sprawdzanie zmian co 3 min
  (`INTERVAL=3`); `ONCE` – jedno przejście.
- **Konfiguracja zapamiętywana (użytkownik, 13.10.2026):** po uruchomieniu z `LOCAL`/`REMOTE`
  ustawienia są zapisywane, potem wystarczy samo `AmiDrive`.
- **Ikona (użytkownik, 13.10.2026):** start z Workbencha, ustawienia także w Tool Types.
- **Miejsca danych (użytkownik, 13.10.2026):** ustawienia `ENVARC:AmiDrive/AmiDrive.prefs`,
  token `ENVARC:AmiDrive/token`, stan + pliki tymczasowe + dziennik w `<LOCAL>/.AmiDrive/`
  (`STATE=` zmienia). Uzasadnienie i tabela w `CLAUDE.md`, sekcja „Stan synchronizacji”.

- **Weryfikacja certyfikatów zawsze włączona** (przykłady z SDK AmiSSL jej nie mają). Łańcuch
  z `AmiSSL:Certs`, `SSL_set1_host`, SNI, minimum TLS 1.2. Przez te połączenia idą tokeny.
- **Keep-alive**: nawiązanie TLS kosztuje sekundy (na 68030 więcej), kolejne zapytanie
  dziesiątki ms. Połączenie trzymane otwarte, nowe tylko po `Connection: close` lub błędzie.
- **Strefa czasowa**: najpierw zmienna `TZ` (POSIX, z regułami czasu letniego), potem stałe
  przesunięcie z Prefs/Locale. Ustawienia regionalne OS 3.2 nie znają czasu letniego (tylko
  `lp_GMTOffset`), więc same dawałyby latem błąd 1 h. Na A1200: `TZ=CET-1CEST,M3.5.0,M10.5.0/3`,
  w Locale zostaje GMT+1 (decyzja użytkownika).
- **Czas** jako `unsigned long` sekund od 1978-01-01 UTC (epoka Amigi): zakres do 2114 r.,
  bez arytmetyki 64-bit.
- **Rozmiary plików**: `unsigned long`, czyli do 4 GB; większe pliki `json_ulong` zgłasza jako
  `AD_ERR_TOOBIG` – do obsłużenia w `sync.c` (pominięcie z komunikatem).
- **Kodowanie nazw**: `CHARSET=LATIN1` (domyślnie) lub `LATIN2` (polskie litery). Znaki spoza
  strony kodowej → `_`, za długie nazwy → `<początek>~hhhhhh<.ext>` (FNV-1a pełnej nazwy).
  Gdy nazwa się zmienia, `sync.c` zapisze parę nazw w stanie; przy kolizji `force_hash`.
- **JSON**: jsmn potrzebuje całej odpowiedzi w pamięci. Strony delta trzeba więc zmniejszyć:
  `$select` tylko z potrzebnymi polami, umiarkowany rozmiar strony; jedna strona naraz.
- **Moduł `tz.c`** wydzielony z `fsmap.c` (daty i strefy to osobna, dobrze testowalna logika).
- **Graph: elementy po `id`** (`graph.c`): odpowiedzi delta nie zawierają `parentReference.path`
  (zmiana nazwy folderu nie zwraca jego zawartości), więc stan śledzi `id` i `parentReference.id`,
  a ścieżki lokalne składa `sync.c`. Czas pliku z `fileSystemInfo.lastModifiedDateTime` (czas
  nadany przez program, który zapisał plik), gdy brak – czas serwera.
- **Strona delta w pamięci w całości**: `$select` z potrzebnymi polami, `$top=20`; bufor 48 KB
  + 2048 żetonów jsmn (32 KB). Pominięte elementy: bez `id`; flagi dla `package` (OneNote),
  `remoteItem` (udostępnione z innego dysku), ≥ 4 GB, nazw dłuższych niż bufor.
- **Ponowienia**: 429, 500, 502, 503, 504 – czekanie według `Retry-After` (najwyżej 10 min),
  bez niego 2, 4, 8, 16 s; 5 prób; Ctrl-C przerywa czekanie. 404 → `AD_ERR_NOTFOUND`,
  410 → `AD_ERR_GONE` (pełna delta od nowa), 401 → `AD_ERR_AUTH` (odświeżyć token, powtórzyć).
- **Pobieranie**: `/content` odpowiada 302 na adres serwera plików (inny host, osobna sesja
  z keep-alive). Tam **bez tokenu** – adres sam daje dostęp; tylko `https://`, port 443.
- **Stan** (`state.c`): wpis = `id`, `parent`, nazwa zdalna (UTF-8) i lokalna, `cTag`, `eTag`,
  rozmiar, czas zdalny (UTC) i czas pliku lokalnego po synchronizacji (do wykrywania zmian na
  Amidze w etapie 2). **Bez pełnych ścieżek** – zmiana nazwy folderu w delcie przychodzi tylko
  dla samego folderu, więc ścieżki składane z rodziców (`state_local_path`/`state_remote_path`).
  Wpis to jeden blok `malloc` (ok. 100–150 bajtów), tablica mieszająca 512 kubełków; 5000
  plików ≈ 0,7 MB. Plik tekstowy (`STATE_MAGIC "AmiDrive state 1"`), pola rozdzielone
  tabulatorem z ucieczkami `\t \n \r \\`, na końcu `end <liczba>`: plik ucięty lub uszkodzony
  → pusty stan i pełna delta (nie ryzykujemy błędnego stanu).
- **Synchronizacja, etap 1** (`sync.c`):
  - elementy delty stosowane od razu, po kolei (stary stan daje dotychczasową ścieżkę przy
    przenoszeniu); element, którego folder jeszcze nie przyszedł, czeka do końca strony;
  - pobieranie do `<katalog>/.AmiDrive.part` (ten sam wolumin, nazwa mieści się w 30 znakach),
    data pliku = `fileSystemInfo` przeliczona na czas lokalny, potem usunięcie starej wersji
    i zmiana nazwy (AmigaDOS nie nadpisuje przy `Rename`); rozmiar musi się zgadzać;
  - **pliki z Amigi nie giną**: plik zmieniony lokalnie (rozmiar lub data inne niż po ostatniej
    synchronizacji) przy nowej wersji z chmury dostaje nazwę `nazwa-Amiga.ext`, przy usunięciu
    w chmurze zostaje; folder z czymś w środku zostaje; plik lokalny równy chmurze (rozmiar
    i data) jest przejmowany bez pobierania – także pierwsza synchronizacja na istniejące pliki
    i powrót po przerwaniu między podmianą a zapisem stanu;
  - nazwa, którą trzeba było zmienić (znaki spoza strony kodowej, za długa), zawsze z hashem –
    nie zderzy się z prawdziwą nazwą tej samej postaci;
  - **nowy deltaLink tylko po bezbłędnym przejściu**; inaczej następne przejście czyta te same
    zmiany (ponowne zastosowanie nic nie psuje), więc nieudany plik nie ginie;
  - błędy sieci, Ctrl-C, odrzucone logowanie, przeciążenie po 5 próbach przerywają przejście
    (stan zapisany); brak pliku, błąd zapisu lokalnego – tylko ten element;
  - pełna lista (pierwsze przejście, po 410, inny folder): elementy stanu nieobecne na liście
    są usuwane według tych samych zasad; stan innego folderu jest porzucany (pliki zostają);
  - stan zapisywany co najwyżej co 30 s w trakcie i na końcu, z dotychczasowym deltaLinkiem;
  - pomijane: notesy OneNote, elementy udostępnione z innego dysku, pliki ≥ 4 GB, nazwy
    zarezerwowane (`.AmiDrive` w korzeniu, `.AmiDrive.part`).
- **Zmiana sposobu nazw** (`CHARSET`, limit długości): stan pamięta `names` (np. `LATIN2/30`);
  przy zmianie jedno pełne przejście, a zwykłe przenoszenie poprawia nazwy lokalne bez pobierania.
- **Limit nazw** z typu woluminu (`Info`): OFS/FFS `DOS\0`–`DOS\5` → 30, reszta (FFS2, SFS, PFS)
  → 107. Na A1200 `SYS:` i `RAM:` dają 30 (bezpiecznie: najwyżej krótsze nazwy).
- **Rejestracja aplikacji** przez Azure CLI (`az ad app create` + `az rest PATCH` na
  `api.requestedAccessTokenVersion=2` i `signInAudience=AzureADandPersonalMicrosoftAccount`;
  portal Entra kierował logowanie do obcej dzierżawy „Microsoft Services”, AADSTS50020).
  Identyfikator wpisany jako domyślny `AMIDRIVE_CLIENT_ID`; `CLIENTID=` dla organizacji
  z własną rejestracją. Konta firmowe innych organizacji mogą wymagać zgody administratora
  albo weryfikacji wydawcy (Publisher verification).
- Program kompilowany dla 68020 (`-cpu=68020`), działa na 68020–68060.

## Znane ograniczenia / do zrobienia

- Sprawdzone z kontem Business: `deltaLink` zachowuje `$select` i `$top`; delta na podfolderze
  działa (pusty folder, potem przyrostowo 5 plików). Pusty folder w Business nie zwraca
  w delcie samego siebie (nie szkodzi – korzeń bierzemy z `graph_item`).
- Etap 1: plik usunięty na Amidze nie jest pobierany ponownie, dopóki nie zmieni się w chmurze
  (etap 2 zamieni to na usunięcie w chmurze).
- Element, którego folder nadrzędny nie należy do synchronizowanego folderu (np. przeniesiony
  na zewnątrz, jeśli OneDrive nie zgłosi go jako usuniętego), liczony jako błąd – deltaLink stoi.
  Do sprawdzenia z prawdziwym kontem.
- Pobieranie nie wznawia przerwanych plików (nagłówek `Range`) – plik pobierany od nowa.

- Nazwy w Unicode NFD (np. z macOS: litera + znak łączący) dają `_` zamiast litery z ogonkiem –
  można dodać składanie najczęstszych znaków.
- `Retry-After` w postaci daty HTTP nie jest obsługiwane (Graph podaje sekundy).
- `gethostbyname` tylko IPv4.
