# CLAUDE.md — AmiDrive: klient OneDrive dla Amigi

Natywny klient OneDrive dla AmigaOS, który synchronizuje **jeden wybrany folder** z OneDrive
z katalogiem na dysku Amigi. Nie synchronizuje całego dysku w chmurze.

## Cele i zakres

- Platformy docelowe: AmigaOS 3.x (68020+, realnie 68030/68060, PiStorm, Vampire), docelowo także AmigaOS 4 i MorphOS.
- Sieć: dowolna karta działająca przez SANA-II (WiFi Prism2/PCMCIA, X-Surf, Plipbox itp.).
- Etap 1: tylko pobieranie zmian (chmura → Amiga).
- Etap 2: wysyłanie (Amiga → chmura) i rozwiązywanie konfliktów.
- Poza zakresem: synchronizacja całego dysku, GUI w pierwszej wersji (najpierw narzędzie CLI uruchamiane z Shella lub z harmonogramu).

## API: Microsoft Graph

Wszystkie wywołania idą przez HTTPS do `https://graph.microsoft.com/v1.0`.

- Lista plików w folderze: `GET /me/drive/root:/{ścieżka}:/children`
- Wykrywanie zmian: `GET /me/drive/root:/{ścieżka}:/delta`. Pierwsze wywołanie zwraca pełny stan,
  kolejne zaczynają od zapisanego `@odata.deltaLink` i zwracają tylko różnice.
  Delta na podfolderze działa w OneDrive Personal, a w OneDrive for Business tylko od korzenia
  (wtedy trzeba filtrować wyniki po `parentReference.path`).
- Pobieranie: `GET /me/drive/items/{id}/content` odpowiada przekierowaniem 302 na wstępnie
  autoryzowany URL. Obsłużyć przekierowanie i pobierać strumieniowo prosto na dysk.
- Wysyłanie małych plików: `PUT /me/drive/root:/{ścieżka}/{nazwa}:/content`.
- Wysyłanie dużych plików: `POST .../createUploadSession`, potem fragmenty `PUT` z nagłówkiem
  `Content-Range` (rozmiar fragmentu musi być wielokrotnością 320 KiB).
- Obsługiwać `429` i `503` z nagłówkiem `Retry-After` oraz stronicowanie (`@odata.nextLink`).
- Wykrywanie konfliktów: porównywać `eTag`/`cTag` i `lastModifiedDateTime`.

### Uwierzytelnianie: OAuth 2.0 device code flow

- `POST https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode`
  (dla kont firmowych `organizations` lub `common`).
- Amiga wyświetla `user_code` i adres `verification_uri`; użytkownik loguje się na telefonie lub PC.
- Program odpytuje `/token` co `interval` sekund aż do otrzymania tokenów.
- Zakresy: `Files.ReadWrite offline_access` albo węższy `Files.ReadWrite.AppFolder`
  (ogranicza program do `Apps/AmiDrive`).
- Rejestracja aplikacji w Entra ID jako „public client” (bez sekretu), z włączonym „Allow public client flows”.
- Refresh token przechowywać w pliku konfiguracyjnym (`ENVARC:AmiDrive/`), nigdy nie logować tokenów.

## Stos techniczny na Amidze

- Język: C (C89/C99), kompilator **bebbo-gcc** (m68k-amigaos) lub **vbcc**; dla OS4 `ppc-amigaos-gcc`.
- Sieć: `bsdsocket.library` (Roadshow zalecany; AmiTCP/Genesis, Miami też powinny działać).
- TLS: **AmiSSL 5** (OpenSSL 3.x, TLS 1.2/1.3). Otwierać `amisslmaster.library`, inicjować per proces.
  Na 68030 handshake trwa kilka sekund — utrzymywać połączenie (HTTP keep-alive) zamiast otwierać nowe.
- HTTP: własny minimalny klient HTTP/1.1 (chunked transfer, 302, keep-alive). Bez zewnętrznych dużych bibliotek.
- JSON: **jsmn** (bez alokacji) lub cJSON. Odpowiedzi parsować przyrostowo, nie trzymać całych dużych odpowiedzi w RAM.
- Pamięć: celować w działanie przy 2–4 MB Fast RAM. Stałe bufory, brak rekurencyjnego ładowania całego drzewa.

## Mapowanie plików

- Nazwy: OFS/FFS mają limit 30 znaków, SFS/PFS/FFS2 ~107. Długie nazwy skracać deterministycznie
  i zapisywać mapowanie w bazie stanu.
- Kodowanie: OneDrive używa UTF-8, Amiga ISO-8859-1 (lub inna strona kodowa). Konwertować w obie strony;
  znaki nieprzekładalne zamieniać i zapisywać mapowanie.
- Znaki zakazane w OneDrive: `" * : < > ? / \ |` oraz nazwy kończące się spacją lub kropką.
- Czas: Graph zwraca UTC (ISO 8601), Amiga trzyma czas lokalny (DateStamp od 1978-01-01).
  Uwzględniać strefę czasową z `locale.library`.
- Atrybuty Amigi (protection bits, komentarze plików) nie istnieją w OneDrive — nie synchronizować ich.

## Stan synchronizacji

Miejsca danych (uzgodnione z użytkownikiem 13.10.2026):

| Co | Domyślnie | Zmiana |
|---|---|---|
| synchronizowane pliki | `LOCAL=` | argument / Tool Type, zapamiętywany |
| ustawienia (tekst `KLUCZ=wartość`) | `ENVARC:AmiDrive/AmiDrive.prefs` | argumenty Shella > Tool Types > plik |
| token logowania (tajny) | `ENVARC:AmiDrive/token` | stałe; nigdy obok danych |
| stan, pliki w trakcie pobierania, dziennik | `<LOCAL>/.AmiDrive/` | `STATE=` |

Pliki tymczasowe muszą być na tym samym woluminie co docelowe (`Rename()` nie działa między
woluminami), stan nie może być w `ENVARC:` (kopiowany do RAM przy starcie). Katalog `.AmiDrive`
jest pomijany przy synchronizacji. Domyślnie praca w tle co 3 min (`INTERVAL=3`), `ONCE` – jedno
przejście; ustawienia podane w Shellu są zapisywane; start z ikony (Tool Types, opcja `WINDOW`,
dziennik `.AmiDrive/AmiDrive.log`).

Plik stanu zapisywany atomowo (zapis do pliku tymczasowego, potem rename):
- ostatni `deltaLink`,
- dla każdego pliku: id elementu OneDrive, ścieżka lokalna, ścieżka zdalna, eTag, rozmiar, data modyfikacji.

## Proponowane moduły

- `auth.c` — device code flow, odświeżanie tokenu, zapis/odczyt tokenów.
- `net.c` — gniazda + AmiSSL, połączenie, keep-alive, timeouty.
- `http.c` — żądania/odpowiedzi HTTP/1.1, przekierowania, chunked.
- `graph.c` — wywołania Graph (children, delta, download, upload, upload session).
- `json.c` — cienka warstwa nad jsmn.
- `state.c` — baza stanu synchronizacji.
- `fsmap.c` — konwersja nazw, kodowania, dat.
- `sync.c` — logika synchronizacji i konfliktów.
- `main.c` — argumenty CLI (ReadArgs), konfiguracja, logowanie.

## Konwencje

- Argumenty CLI przez `ReadArgs()`, styl Amigi (np. `AmiDrive LOCAL=DH1:OneDrive REMOTE=/Amiga ONCE/S`).
- Kody wyjścia: `RETURN_OK`, `RETURN_WARN`, `RETURN_ERROR`, `RETURN_FAIL`.
- Obsługa Ctrl-C (`SIGBREAKF_CTRL_C`) w każdej długiej pętli; przerwanie nie może uszkodzić stanu.
- Zawsze zwalniać zasoby (biblioteki, gniazda, pamięć) — brak ochrony pamięci na OS3.
- Testy logiki niezależnej od systemu (JSON, mapowanie nazw, sync) kompilować też na Linuksie/macOS.
- Testy na Amidze: emulator (FS-UAE/WinUAE z Roadshow i AmiSSL) przed testami na prawdziwym sprzęcie.

## Budowa, testy, Amiga (stan prac: `docs/STATUS.md`)

- Budowa w MSYS2 UCRT64: `make` → `build/amiga/AmiDrive`, `make tests` → `build/amiga/AmiDriveTests`,
  `make -f Makefile.host test` → testy na PC (gcc). SDK AmiSSL 5.24 w `C:\amiga-dev\amissl`.
- Prawdziwa A1200 przez AmiAgent, katalog **`Work:AmiDrive`**. `tools/amiga_remote.py` uruchamiać
  z Git Bash / PowerShell (Python ze Sklepu Windows nie działa z powłoki MSYS2):
  `push`, `run "Work:AmiDrive/AmiDrive TEST"`, `push --local build/amiga/AmiDriveTests` + `run`.
- Moduły przenośne (`http`, `json`, `fsmap`, `tz`, `session`, `config`, `auth`, `graph`) bez
  nagłówków Amigi; system tylko przez `net.h` i `plat.h`. Zależne od Amigi: `main.c`,
  `net_amiga.c` (`net.c` z listy wyżej), `plat_amiga.c`. W testach atrapy `tests/fake_net.c`
  (scenariusz odpowiedzi na każde połączenie) i `tests/fake_plat.c` (zegar, pliki w pamięci). Każdą zmianę modułu przenośnego testować na PC
  i na Amidze (big endian, vbcc).
- Daty i strefy w `tz.c` (wydzielone z `fsmap.c`): strefa najpierw z `TZ` (POSIX, czas letni),
  potem z Locale (OS 3.2 nie zna czasu letniego).
- Certyfikaty TLS zawsze weryfikowane; nie wyłączać (przez połączenia idą tokeny).

## Alternatywa (gdy natywny klient nie jest potrzebny)

Raspberry Pi lub PC z `rclone` synchronizuje wybrany folder OneDrive i udostępnia go Amidze przez SMB
(smb2fs) lub FTP. Amiga nie potrzebuje wtedy TLS ani OAuth.
