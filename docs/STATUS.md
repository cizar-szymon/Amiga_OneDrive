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
| Start z ikony (Tool Types, `WINDOW`, logowanie w oknie, pytanie o zatrzymanie), dziennik `.AmiDrive/AmiDrive.log`, jedna synchronizacja naraz, `STOP`, ikona `AmiDrive.info` | ✅ A1200 (`WBRun`), także logowanie z ikony po poprawce `net_init` |
| `state.c`: baza stanu po `id` (ścieżki z łańcucha rodziców), plik tekstowy zapisywany strumieniowo do `.new` i podmieniany, linia końcowa | ✅ testy + A1200 (`AmiDrive TEST` zapisuje i czyta stan w `T:`) |
| `plat`: strumieniowe pliki (`plat_open_read/write`, `plat_close` z podmianą) | ✅ A1200 |
| `sync.c`: etap 1 – pobieranie zmian (foldery, pobieranie, przenoszenie, usuwanie, ochrona plików zmienionych na Amidze, pełna lista po 410) | ✅ testy + A1200 z kontem Business (nowe pliki, usuwanie, zmiana nazwy, podfolder, 3 poziomy folderów, przeniesienia plików i folderów, zmiana nazwy folderu, nazwa > 30 znaków, przeniesienie poza folder) |
| `plat`: informacja o pliku, data, zmiana nazwy, tworzenie wprost, limit nazw woluminu | ✅ A1200 (`AmiDrive TEST`) |
| `AmiDrive` / `AmiDrive ONCE`: synchronizacja co `INTERVAL` min albo raz, Ctrl-C, podsumowanie | ✅ A1200: `ONCE` i praca w tle (`INTERVAL=1`, zmiany wykryte w kolejnych cyklach, Ctrl-C w czasie czekania) |
| Ikona „OneDrive” na pulpicie (AppIcon): dwuklik otwiera katalog, upuszczone pliki kopiowane i wysyłane, menu Narzędzia „synchronise now” / „stop”, `APPICON=YES\|NO` | ✅ A1200: ikona dodawana i zdejmowana; ⏳ dwuklik, upuszczanie, menu (wymaga myszy) |
| Autostart: ikona projektu `SystemSD:WBStartup/AmiDrive.info` + plik `AmiDrive` (narzędzie domyślne `Work:AmiDrive/AmiDrive`, `DONOTWAIT`) | ✅ A1200: otwarcie ikony uruchamia AmiDrive w tle; restart (`C:Reboot`, 14.10.2026): AmiDrive wystartował sam, ikona „OneDrive” na pulpicie, przejście „no changes” |
| Instalator (`install/Install-AmiDrive`, Installer 47) i paczka `releases/amiga/AmiDrive-0.3.0.lha` (`tools/make_release.py --lha`) | ✅ A1200: aktualizacja na prawdziwo (EN), nowa instalacja w trybie symulacji (PL) |
| Zegar Amigi: czas z nagłówka `Date` serwera Graph (TLS z weryfikacją łańcucha i nazwy, bez dat), okno „Set clock / Always set automatically / Cancel”, `CLOCK=ASK\|AUTO\|NO`, `SETCLOCK`, pytanie w instalatorze (0.3.1) | ✅ testy + A1200 |
| Okno postępu (0.3.2): `SyncProgress` w `sync.c` (faza, plik, bajty, całość przy pełnej liście i wysyłaniu), okno z paskiem, prędkością i czasem do końca, stan między przejściami; ikona i menu obsługiwane w trakcie przejścia | ✅ testy + A1200 |
| Przyciski w oknie AmiDrive (0.3.3): „Synchronise now” (wyszarzony w trakcie), „Stop AmiDrive”, „Hide” (GadTools); dwuklik na OneDrive zawsze otwiera szufladę i okno | ✅ A1200 |
| Szybkość TLS (0.3.4): szyfry ChaCha20 / AES-128 zamiast AES-256, `TEST URL=` (http bez TLS / https, ten sam klient), mniejsze okno („Sync now / Stop / Hide”) | ✅ A1200 |
| Szuflada z plikami (0.3.5): `OpenWorkbenchObject` z `WBOPENA_Show=DDFLAGS_SHOWALL`, `WBOPENA_ViewBy=DDVM_BYNAME` (V45); ikona szuflady tworzona, gdy jej brak (te same tryby); 0.3.6: okno AmiDrive w lewym górnym rogu (obok kolumny ikon dysków), szuflada pod nim tej samej szerokości (`status_layout`), przyciski węższe, blisko siebie; 0.3.7: okno tak szerokie jak przyciski (238 px wnętrza), krótsze napisy, rysowanie bez czyszczenia (JAM2, bez migania); 0.3.8: położenie okna szuflady ustawiane w jej ikonie przed każdym otwarciem (także istniejącej, np. z instalatora – u użytkownika okno było obok), „Last check … / Next at …” | ✅ A1200 |
| Etap 2: wysyłanie (sesje w kawałkach 320 KiB), foldery, nowe wersje z `If-Match`, zmiana nazwy/przeniesienie przez `PATCH`, usuwanie, konflikty, `UPLOAD=YES\|NO` | ✅ testy + A1200 (atrapy) + A1200 z kontem Business (nowy plik i folder, nowa wersja, zmiana nazwy, przeniesienie, konflikt, usunięcie, zmiana nazwy i usunięcie folderu) |

## Wyniki testów

- PC (`make -f Makefile.host test`, gcc `-Wall -Wextra -pedantic`): 1293 sprawdzeń, 0 błędów.
- A1200 (`AmiDriveTests`, ten sam zestaw skompilowany vbcc): 1293 sprawdzeń, 0 błędów.
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
  pliku („Kopiuj do”) = nowy element, pobrany.
  Dalej (14.10.2026, zmiany w chmurze drugą kopią `LOCAL=RAM:Check` i w przeglądarce):
  3 poziomy folderów z plikami → utworzone i pobrane po kolei; pliki przeniesione między
  poziomami → przeniesione lokalnie bez pobierania; zmiana nazwy folderu `Poziom1` → `PoziomA`
  i przeniesienie folderu `Poziom2` (z `Poziom3` i plikiem) wyżej → jedno przeniesienie folderu,
  zawartość idzie z nim; nazwa 55 znaków → `To jest bardzo dlug~824513.txt` (30 znaków na `Work:`),
  usunięcie tego pliku na Amidze usunęło właściwy element w chmurze; plik przeniesiony poza
  synchronizowany folder → OneDrive zgłasza go jako usunięty, usunięty lokalnie, bez błędu.
- A1200, etap 2 (14.10.2026): pliki dodane na Amidze (`Copy_of_WhichAmiga` 20 KB + `.info`)
  wysłane do `Retro/AmigaDrive`; następne przejście „no changes” (własne wysyłki wracają
  w delcie z tym samym cTag).
  Dalej (14.10.2026), każdy krok `ONCE` i potem drugie przejście „no changes”; stan chmury
  sprawdzany drugą kopią (`LOCAL=RAM:Check`, osobny stan, ta sama chmura – działała też jako
  drugi klient do konfliktów):
  - nowy plik `test-up.txt` → wysłany; zmiana treści → „Uploaded new version” (`If-Match`);
  - zmiana nazwy na Amidze i przeniesienie do `podfolder` → `PATCH` bez wysyłania; w chmurze
    poprawna treść i data;
  - konflikt (plik zmieniony w drugiej kopii i na Amidze): wersja z chmury pobrana pod
    właściwą nazwą, wersja z Amigi jako `test-moved-Amiga.txt` wysłana jako nowy plik;
  - plik usunięty na Amidze, a zmieniony w chmurze → wraca z chmury;
  - usunięcie plików → usunięte w chmurze; nowy folder z plikiem → folder, potem plik;
    zmiana nazwy folderu → nowy folder + przeniesienie pliku + usunięcie starego (zgodnie
    z opisem w ograniczeniach); usunięcie folderu z zawartością → jedno usunięcie folderu.
  Na koniec chmura w stanie sprzed testów (kopia w `RAM:Check` usunęła pliki testowe).
- A1200, praca w tle i start z ikony (14.10.2026, wersja 0.2.0):
  - `AmiDrive INTERVAL=1` w tle: przejście co minutę, plik dodany na Amidze wysłany w następnym
    cyklu, usunięty – usunięty w chmurze; Ctrl-C (`stop`) kończy od razu;
  - druga kopia z Shella: „already running”; `AmiDrive STOP` zatrzymuje i czeka na koniec;
  - start jak z ikony (`tools/amiga/WBRun.c`, `OpenWorkbenchObject`): bez okna, wpisy tylko
    w dzienniku; ponowny start → pytanie „Stop it?” (Esc = Anuluj; Return nie wybiera przycisku);
  - Tool Types `WINDOW ONCE "LOCAL=RAM:WB Check" UPLOAD=NO TOOLPRI=0`: okno z postępem, wartość
    ze spacją, `TOOLPRI` pominięte, prefs bez zmian;
  - start z ikony bez tokenu: logowanie w oknie, potem synchronizacja – **padło** („Memory header
    not located”): `cmd_login` i `cmd_sync` wywoływały `net_init()` dwa razy (druga inicjalizacja
    AmiSSL w tym samym procesie, połączenie z pierwszej). Poprawione: drugie wywołanie nic nie robi.
    Po poprawce (prawdziwa ikona `Work:AmiDrive/AmiDrive`, Tool Types wyłączone): kod w oknie,
    logowanie, przejście „no changes” tylko w dzienniku, dalej praca w tle; kopia z Shella
    „already running”.
- A1200, instalator (14.10.2026, sterowany zdalnie programem `tools/amiga/Click.c`):
  - aktualizacja na prawdziwo, Workbench po angielsku: `Work:` → `AmiDrive STOP` (dziennik:
    „Stopped”), kopiowanie programu i opisów, „Keep” ustawień, TZ pominięte (ustawione),
    autostart (ikona w `SYS:WBStartup` z narzędziem `Work:AmiDrive/AmiDrive`), „Start” →
    `openwbobject` uruchomił AmiDrive (ikona „OneDrive”, przejście „no changes”);
  - błąd znaleziony: opisy kopiowane bez ikon (wzorzec `#?.(txt|pdf)`) → teraz `(all)`;
  - nowa instalacja po polsku (`LANGUAGE=polski`) w trybie symulacji: wszystkie pytania
    (katalog plików, nazwa, folder OneDrive, Latin-2, minuty, kierunek, ikona, autostart,
    start), ustawienia i `WBStartup` bez zmian; polecenie zapisu ustawień sprawdzone ręcznie;
  - Installer z Shella spoza swojej szuflady nie widzi plików paczki → teraz czytelny
    komunikat na początku (z ikony katalogiem bieżącym jest szuflada paczki);
  - czcionka Workbencha na A1200 nie ma części liter Latin-2 (krzaki także w tekstach
    samego Installera) – sprawa czcionki systemu, nie skryptu.
- A1200, zegar (14.10.2026, 0.3.1; zgłoszenie: na innej Amidze logowanie kończyło się
  „certificate rejected: certificate is not yet valid” – zły zegar):
  - `SETCLOCK` przy dobrym zegarze: różnica 3 s, ustawione razem z zegarem z baterią;
  - `Date 14-Oct-25` + `ONCE` (CLOCK=ASK): okno przy starcie (zegar przed datą wydania),
    „365 days behind”, „Set clock” → zegar dobry, przejście „no changes”;
  - `Date 14-Oct-30` + `ONCE`: odrzucony certyfikat („certificate has expired”, z podpowiedzią
    o zegarze) → okno „1460 days ahead” → „Always set automatically” → zegar dobry,
    `CLOCK=AUTO` w prefs, ponowione przejście bez błędu;
  - CLOCK=AUTO, zegar +1 h: ustawiony bez okna, wpis w dzienniku.
- A1200, okno postępu (14.10.2026, 0.3.2; zgłoszenie: dwuklik na „OneDrive” w trakcie długiej
  synchronizacji nie otwierał szuflady aż do końca – wyglądało na zawieszenie): kopia
  testowa `LOCAL=RAM:Big` z pustym stanem (8,9 MB): dwuklik w trakcie → szuflada od razu
  + okno „Getting the changes from OneDrive: 5 %”, plik 672 KB z 3,5 MB, 695 KB z 8,9 MB,
  41 KB/s, „about 3 min left”; po końcu okno samo przeszło w „Up to date”, wynik i godzina
  następnego sprawdzenia.
- A1200, przyciski (14.10.2026, 0.3.3; prośba: „prawy przycisk na ikonie: stop, synchronizuj
  teraz”): Workbench 3.2 nie ma menu kontekstowego ikon (prawy przycisk zawsze pokazuje pasek
  menu; programy mogą dodać tylko pozycje do Tools – są tam), więc wybrany wariant: przyciski
  w oknie. Dwuklik w czasie czekania → szuflada + okno; „Synchronise now” → przejście
  (przycisk wyszarzony w trakcie), potem „Up to date”; „Stop AmiDrive” → program zakończony,
  ikona zniknęła.
- A1200 `AmiDrive TEST`: TLS 1.3 `TLS_AES_256_GCM_SHA384`, nawiązanie połączenia z
  `graph.microsoft.com` ok. 2,3 s, drugie zapytanie na tym samym połączeniu 80 ms.
- A1200 `AmiDrive TEST HOST=...` (badssl.com): odrzucone `expired`, `wrong.host`, `self-signed`,
  `untrusted-root`; przyjęty `badssl.com`.

## Wydajność (A1200, 68060/50, Ethernet Warp, 14.10.2026)

| Pomiar | Wynik |
|---|---|
| `openssl speed` (AmiSSL 5.24 / OpenSSL 3.5.4), bloki 16 KB | AES-128-GCM 298 KB/s, AES-256-GCM 261 KB/s, ChaCha20-Poly1305 458 KB/s |
| `wget` http 5 MB do `RAM:` | 1,15 MB/s (chwilami 3 MB/s) |
| `AmiDrive TEST URL=` cachefly 5 MB, http (bez TLS) | 1590 KB/s |
| to samo, https, AES-256-GCM (wybór serwera przy dawnej kolejności) | 215 KB/s |
| to samo, https, ChaCha20 (nowa kolejność) | 337 KB/s |
| Microsoft (Graph, login, SharePoint) przy nowej kolejności | wybiera AES-128-GCM (wcześniej AES-256) |
| OneDrive, pełna synchronizacja 20,7 MB do `RAM:` | AES-256: 130 s (159 KB/s), AES-128: 108 s (192 KB/s) |

Wniosek: przy szyfrowaniu ogranicza procesor (AES bez instrukcji sprzętowych), sieć i stos
TCP dają ponad 1,5 MB/s. TLS 1.3: oferowane tylko `TLS_CHACHA20_POLY1305_SHA256` i
`TLS_AES_128_GCM_SHA256` (AES-128 jest obowiązkowy w TLS 1.3, więc każdy serwer go ma);
TLS 1.2: ECDHE z ChaCha20 / AES-GCM. Nawiązanie połączenia 2,7–12 s (RSA/ECDHE na 68060),
dlatego keep-alive.

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
- **Etap 2 – wysyłanie** (`sync.c`, po etapie 1 w tym samym przejściu):
  - przegląd `LOCAL` (pomija `.AmiDrive` w korzeniu i `.AmiDrive.part`), porównanie ze stanem
    po (rodzic, nazwa lokalna): brak wpisu = nowy, inny rozmiar lub data = zmieniony,
    wpis bez pliku = usunięty na Amidze;
  - kolejność: nowe foldery i pliki (foldery przed zawartością), potem nowe wersje, potem
    usunięcia; nowy plik o rozmiarze i dacie brakującego = zmiana nazwy / przeniesienie
    (`PATCH`, bez wysyłania);
  - **konflikty bez utraty danych**: zmiany z chmury idą pierwsze, plik zmieniony w obu
    miejscach dostaje kopię `-Amiga` (wysyłaną potem jako nowy plik); nowa wersja tylko
    z `If-Match: eTag` (412 → zostaje do następnego przejścia); nowy plik z
    `conflictBehavior=fail` (409 → w następnym przejściu przychodzi wersja z chmury,
    lokalna dostaje `-Amiga`); usunięcie z `If-Match` (zmieniony w chmurze → nie usuwa,
    wraca z chmury);
  - **ochrona przed masowym usuwaniem**: gdy brakuje > 10 elementów i ponad połowy, nic nie
    jest usuwane (ostrzeżenie); gdy katalogu nie da się przeczytać w całości – nic nie
    jest wysyłane ani usuwane; usunięte trafiają do kosza OneDrive;
  - wysyłanie: `createUploadSession` z datą pliku w `fileSystemInfo`, kawałki 320 KiB
    (bufor 320 KB tylko na czas wysyłania) do serwera wysyłania **bez tokenu**; pusty plik
    `PUT` + `PATCH` daty; nazwy z Amigi → UTF-8, nazwy niedozwolone w OneDrive pomijane
    z komunikatem; bity ochrony i komentarze nie są przenoszone;
  - zmiany w chmurze wywołane własnymi wysyłkami wracają w delcie z tym samym `cTag`
    i niczego nie zmieniają.
- **Limit nazw** z typu woluminu (`Info`): OFS/FFS `DOS\0`–`DOS\5` → 30, reszta (FFS2, SFS, PFS)
  → 107. Na A1200 `SYS:` i `RAM:` dają 30 (bezpiecznie: najwyżej krótsze nazwy).
- **Rejestracja aplikacji** przez Azure CLI (`az ad app create` + `az rest PATCH` na
  `api.requestedAccessTokenVersion=2` i `signInAudience=AzureADandPersonalMicrosoftAccount`;
  portal Entra kierował logowanie do obcej dzierżawy „Microsoft Services”, AADSTS50020).
  Identyfikator wpisany jako domyślny `AMIDRIVE_CLIENT_ID`; `CLIENTID=` dla organizacji
  z własną rejestracją. Konta firmowe innych organizacji mogą wymagać zgody administratora
  albo weryfikacji wydawcy (Publisher verification).
- **Start z ikony (14.10.2026):** Tool Types czytane tym samym `ReadArgs` (`RDA_Source`), tylko
  klucze z szablonu (obce, np. `TOOLPRI`, `DONOTWAIT`, pomijane), wartości w cudzysłowach;
  pierwszeństwo: Shell > Tool Types > plik, zapisywane tylko argumenty Shella. Okno
  `CON:…/AUTO/CLOSE/WAIT` otwiera się dopiero przy wypisaniu czegoś: błędy i logowanie zawsze,
  postęp synchronizacji tylko z `WINDOW` (inaczej tylko dziennik). Bez tokenu – logowanie od razu.
- **Jedna synchronizacja naraz:** publiczny port `AmiDrive` (dwie kopie psułyby wspólny stan).
  Shell: druga kopia kończy się z komunikatem, `STOP` wysyła Ctrl-C do właściciela portu i czeka
  do 60 s; Workbench: pytanie o zatrzymanie. Wada: proces zawieszony po awarii trzyma port do
  restartu.
- **AppIcon (14.10.2026, użytkownik):** ikona „OneDrive” na pulpicie zamiast `Assign` (Workbench
  nie pokazuje przypisań). Tylko w pracy w tle (nie `ONCE`), obrazek z ikony programu
  (`PROGDIR:<nazwa>.info`), położenie wybiera Workbench. Upuszczone pliki/szuflady kopiuje
  `C:Copy ALL CLONE` (synchronicznie, między przejściami), potem od razu przejście; obiekty
  z wnętrza `LOCAL` i całe woluminy są odrzucane. Czekanie między przejściami co 1/5 s
  sprawdza Ctrl-C i komunikaty ikony.
- **Autostart (14.10.2026):** w `SYS:WBStartup` tylko ikona projektu z narzędziem domyślnym
  `Work:AmiDrive/AmiDrive` (bez drugiej kopii programu; aktualizacja w `Work:AmiDrive`
  wystarcza). Plik `AmiDrive` obok ikony to krótki tekst (ikona projektu potrzebuje pliku).
  Sieć startuje wcześniej (`S:User-Startup` → `S:Network-Startup`).
- **Zegar (14.10.2026, użytkownik):** czas z internetu przez HTTPS do `graph.microsoft.com`
  z `X509_V_FLAG_NO_CHECK_TIME` – łańcuch certyfikatów i nazwa serwera sprawdzane jak zawsze,
  pomijane tylko daty (przy złym zegarze nie da się ich sprawdzić); na tym połączeniu nie idzie
  token. NTP odrzucone: bez uwierzytelnienia i bywa blokowane. Dokładność ok. 1 s.
  ASK pokazuje okno, gdy zegar jest przed datą wydania programu albo certyfikat odrzucono za
  daty; AUTO ustawia przy starcie i co 24 h (różnica ≥ 2 s); ustawiany jest zegar systemu
  (`TR_SETSYSTIME`) i zegar z baterią (`WriteBattClock`). „Always set automatically” zapisuje
  w prefs tylko `CLOCK=AUTO` (nie Tool Types ani argumenty tego uruchomienia).
- **Okno postępu (14.10.2026, użytkownik):** całość znana przy pełnej liście (rozmiar folderu
  z Graph = suma plików; pliki zastane lokalnie liczą się od razu) i przy wysyłaniu (suma
  plików do wysłania); przy zmianach przyrostowych pasek pokazuje bieżący plik. Prędkość
  z bajtów rzeczywiście przesłanych w fazie, czas do końca po 5 s. Okno odświeżane najwyżej
  2 razy na sekundę, z wywołania postępu (każdy kawałek danych, każdy element) – tam też
  obsługa ikony: dwuklik i menu od razu, upuszczone pliki (komunikaty bez odpowiedzi)
  dopiero po przejściu. Samo otwiera się raz przy pełnej liście dłuższej niż 10 s.
- **Dziennik:** `<stan>/AmiDrive.log`, otwierany na czas przejścia (między przejściami można go
  czytać), po 64 KB przenoszony do `AmiDrive.log.old`.
- Program kompilowany dla 68020 (`-cpu=68020`), działa na 68020–68060.

## Znane ograniczenia / do zrobienia

- Sprawdzone z kontem Business: `deltaLink` zachowuje `$select` i `$top`; delta na podfolderze
  działa (pusty folder, potem przyrostowo 5 plików). Pusty folder w Business nie zwraca
  w delcie samego siebie (nie szkodzi – korzeń bierzemy z `graph_item`).
- Zmiana nazwy folderu na Amidze nie jest rozpoznawana jako zmiana nazwy: powstaje nowy
  folder, pliki są do niego przenoszone (`PATCH`, bez wysyłania), stary folder usuwany.
- Wysyłanie nie jest wznawiane po przerwaniu (plik wysyłany od nowa w następnym przejściu).
- `UPLOAD=NO`: plik usunięty na Amidze nie jest pobierany ponownie, dopóki nie zmieni się
  w chmurze.
- Element, którego folder nadrzędny nie należy do synchronizowanego folderu, liczony jako błąd –
  deltaLink stoi. OneDrive for Business przy przeniesieniu na zewnątrz zgłasza element jako
  usunięty (sprawdzone 14.10.2026), więc ten przypadek nie wystąpił.
- Pobieranie nie wznawia przerwanych plików (nagłówek `Range`) – plik pobierany od nowa.

- Nazwy w Unicode NFD (np. z macOS: litera + znak łączący) dają `_` zamiast litery z ogonkiem –
  można dodać składanie najczęstszych znaków.
- `Retry-After` w postaci daty HTTP nie jest obsługiwane (Graph podaje sekundy).
- `gethostbyname` tylko IPv4.
