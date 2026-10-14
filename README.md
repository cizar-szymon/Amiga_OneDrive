# AmiDrive

Natywny klient Microsoft OneDrive dla AmigaOS 3.x. Synchronizuje **jeden wybrany folder**
z OneDrive z katalogiem na dysku Amigi – bez pośrednika (PC, Raspberry Pi, rclone), bezpośrednio
przez internet z szyfrowaniem TLS.

## Opis projektu

Pliki wrzucone do folderu OneDrive na telefonie lub PC same pojawiają się na Amidze: dyskietki
ADF, moduły, obrazki, dokumenty. AmiDrive działa w tle i co kilka minut pyta OneDrive o zmiany
(tylko o różnice od ostatniego razu), pobiera nowe i zmienione pliki, usuwa i przenosi te, które
zmieniły się w chmurze. Działa też w drugą stronę – zmiany zrobione na Amidze wysyła do chmury,
a przy konflikcie zachowuje obie wersje.

Założenia:

- **mała maszyna:** 68020 lub szybszy, cel 2–4 MB Fast RAM; pliki pobierane strumieniowo prosto
  na dysk, odpowiedzi JSON czytane stronami, stałe bufory;
- **zgodność z systemem:** program Shella i Workbencha (ikona z Tool Types), `ReadArgs`, Ctrl-C
  w każdej długiej pętli, zwalnianie wszystkich zasobów;
- **bezpieczeństwo:** logowanie kodem urządzenia (hasło nigdy nie trafia do Amigi), certyfikaty TLS
  zawsze sprawdzane, token tylko w `ENVARC:AmiDrive/`, nigdy w dzienniku;
- **odporność:** stan zapisywany atomowo, przerwanie (Ctrl-C, reset) nie psuje danych ani stanu;
- **pliki Amigi:** długie nazwy skracane deterministycznie (OFS/FFS 30 znaków), UTF-8 ↔ Latin-1 lub
  Latin-2, czas z uwzględnieniem strefy i czasu letniego (`TZ`).

Technika: C (vbcc), `bsdsocket.library` (Roadshow), AmiSSL 5 (OpenSSL 3), własny klient HTTP/1.1
z keep-alive, parser JSON jsmn, Microsoft Graph API (`/delta`, pobieranie, wysyłanie w sesjach).
Logika niezależna od systemu jest testowana na PC i na Amidze tym samym zestawem testów.

**Stan (wersja 0.2.0):** synchronizacja w obie strony działa na A1200 z prawdziwym kontem
(OneDrive for Business): pobieranie, wysyłanie, przenoszenie, usuwanie, konflikty, praca w tle,
start z ikony.
Szczegóły: [docs/STATUS.md](docs/STATUS.md), założenia: [CLAUDE.md](CLAUDE.md), opis działania
i instalacji dla użytkownika: [docs/AmiDrive-opis.pdf](docs/AmiDrive-opis.pdf) (`tools/make_doc_pdf.py`).

## Wymagania na Amidze

- AmigaOS 3.x, 68020 lub nowszy,
- stos TCP/IP z `bsdsocket.library` v4 (Roadshow, AmiTCP, Miami),
- AmiSSL 5.24 lub nowszy.

## Użycie

```
AmiDrive LOGIN                        logowanie kodem urządzenia (telefon / PC)
AmiDrive LOGOUT                       zapomina logowanie (usuwa token)
AmiDrive INFO                         ustawienia, stan logowania, właściciel i zajętość OneDrive
AmiDrive LOCAL=Work:OneDrive REMOTE=/Amiga [CHARSET=LATIN2] [INTERVAL=3] [UPLOAD=YES|NO]
                                      ustawia i zapamiętuje foldery, synchronizuje
AmiDrive                              praca w tle: synchronizacja co INTERVAL minut, Ctrl-C kończy
AmiDrive ONCE                         jedno przejście
AmiDrive STOP                         zatrzymuje AmiDrive działający w tle (czeka na koniec)
AmiDrive TEST                         sprawdza zegar, strefę czasową, DNS, TLS i HTTP
AmiDrive TEST HOST=badssl.com         to samo dla innego serwera (sprawdzanie certyfikatów)
```

Ustawienia: `ENVARC:AmiDrive/AmiDrive.prefs` (podane w Shellu są zapisywane), token logowania:
`ENVARC:AmiDrive/token` (tajny – nie udostępniać), stan synchronizacji i dziennik
`AmiDrive.log`: `<LOCAL>/.AmiDrive/`. Naraz działa tylko jedna synchronizacja.

**Z ikony (Workbench):** Tool Types to te same opcje co w Shellu (`LOCAL=`, `REMOTE=`, `ONCE`,
`INTERVAL=`…, wyłączone w nawiasach), mają pierwszeństwo przed plikiem ustawień, ale nie są
zapisywane. AmiDrive pracuje w tle bez okna; okno pojawia się tylko przy błędzie albo logowaniu
(pierwszy start bez logowania od razu pokazuje kod), a z Tool Type `WINDOW` pokazuje też postęp.
Ponowne kliknięcie ikony pyta, czy zatrzymać. Ikona w `WBStartup` (ma `DONOTWAIT`) uruchamia
AmiDrive przy starcie systemu.
Opcje `CLIENTID=` i `TENANT=` (`common`, `consumers`, `organizations`) wskazują aplikację w Microsoft
Entra ID, przez którą AmiDrive się loguje.

Strefa czasowa: zmienna `TZ` w formacie POSIX, np. dla Polski
`SetEnv SAVE TZ CET-1CEST,M3.5.0,M10.5.0/3`. Bez niej AmiDrive bierze przesunięcie z Prefs/Locale
(bez czasu letniego).

## Budowanie (Windows)

Potrzebne w `C:\amiga-dev\`: vbcc (`+aos68k_win`), NDK 3.2 (z netinclude Roadshow),
SDK AmiSSL 5.24 w `amissl\` (z [github.com/jens-maus/amissl](https://github.com/jens-maus/amissl/releases)).

W powłoce MSYS2 UCRT64:

```sh
make                          # build/amiga/AmiDrive
make tests                    # build/amiga/AmiDriveTests (testy dla Amigi)
make -f Makefile.host test    # testy modułów przenośnych na PC
```

Ikona (Python z Pillow, z Git Bash / PowerShell; w Git Bash `MSYS_NO_PATHCONV=1`, inaczej
`/Amiga` zamienia się w ścieżkę Windows):

```sh
python tools/make_icon.py build/amiga/AmiDrive.info --tooltypes "(LOCAL=Work:OneDrive)" \
    "(REMOTE=/Amiga)" "(INTERVAL=3)" "(UPLOAD=YES)" "(CHARSET=LATIN2)" "(ONCE)" "(WINDOW)" DONOTWAIT
```

Na Amigę (AmiAgent, katalog `Work:AmiDrive`), z Git Bash albo PowerShell:

```sh
python tools/amiga_remote.py push
python tools/amiga_remote.py run "Work:AmiDrive/AmiDrive TEST"
python tools/amiga_remote.py push --local build/amiga/AmiDriveTests
python tools/amiga_remote.py run "Work:AmiDrive/AmiDriveTests"
```

## Układ

| Plik | Zawartość |
|---|---|
| `src/main.c` | argumenty (`ReadArgs`, Tool Types), polecenia, pętla synchronizacji, dziennik |
| `src/net_amiga.c` | gniazda Roadshow + AmiSSL, weryfikacja certyfikatu |
| `src/plat_amiga.c` | czas, strefa z `TZ` / Locale, czekanie z Ctrl-C, bezpieczny zapis plików |
| `src/auth.c` | logowanie kodem urządzenia, odświeżanie i zapis tokenu (przenośny) |
| `src/graph.c` | wywołania Microsoft Graph (przenośny) |
| `src/session.c` | trwałe połączenie z serwerem, ponowienie zapytania (przenośny) |
| `src/config.c` | plik ustawień (przenośny) |
| `src/http.c` | klient HTTP/1.1 (przenośny) |
| `src/json.c` | warstwa nad jsmn (przenośna) |
| `src/fsmap.c` | nazwy plików i strony kodowe (przenośny) |
| `src/tz.c` | daty ISO 8601, strefy POSIX z czasem letnim (przenośny) |
| `tests/` | testy modułów przenośnych (PC i Amiga) |
| `third_party/jsmn/` | parser JSON jsmn (MIT) |
| `src/sync.c`, `src/state.c` | synchronizacja w obie strony, baza stanu (przenośne) |
| `tools/amiga_remote.py` | klient AmiAgent |
| `tools/make_icon.py`, `tools/icon_tooltypes.py` | ikona `AmiDrive.info`, Tool Types |
| `tools/amiga/WBRun.c` | start programu jak z ikony (testy przez AmiAgent) |
