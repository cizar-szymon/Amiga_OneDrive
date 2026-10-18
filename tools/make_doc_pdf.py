#!/usr/bin/env python3
"""Build docs/AmiDrive-opis.pdf (Polish) and docs/AmiDrive-description.pdf (English): how
AmiDrive works, how it runs on the A1200 now and what another user has to do to use it
with their own OneDrive.

  python tools/make_doc_pdf.py [pl] [en]       (no argument: both)

Needs reportlab and Pillow (pip install reportlab pillow) and the Arial / Consolas fonts
of Windows (Polish letters). The icon is drawn by tools/make_icon.py.
"""

import os
import sys
import tempfile

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (Image, KeepTogether, Paragraph, Preformatted, SimpleDocTemplate,
                                Spacer, Table, TableStyle)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import make_icon  # noqa: E402

FONTS = "C:/Windows/Fonts/"
pdfmetrics.registerFont(TTFont("Arial", FONTS + "arial.ttf"))
pdfmetrics.registerFont(TTFont("Arial-Bold", FONTS + "arialbd.ttf"))
pdfmetrics.registerFont(TTFont("Arial-Italic", FONTS + "ariali.ttf"))
pdfmetrics.registerFont(TTFont("Consolas", FONTS + "consola.ttf"))
pdfmetrics.registerFontFamily("Arial", normal="Arial", bold="Arial-Bold", italic="Arial-Italic",
                              boldItalic="Arial-Bold")

BLUE = colors.HexColor("#3D5A8A")
LIGHT = colors.HexColor("#E8EEF7")
GREY = colors.HexColor("#F3F3F3")

body = ParagraphStyle("body", fontName="Arial", fontSize=10, leading=14, alignment=TA_LEFT,
                      spaceAfter=5)
small = ParagraphStyle("small", parent=body, fontSize=8.5, leading=11, spaceAfter=0)
cell = ParagraphStyle("cell", parent=body, fontSize=9, leading=12, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Arial-Bold", textColor=colors.white)
h1 = ParagraphStyle("h1", parent=body, fontName="Arial-Bold", fontSize=15, leading=19,
                    textColor=BLUE, spaceBefore=12, spaceAfter=6, keepWithNext=1)
h2 = ParagraphStyle("h2", parent=body, fontName="Arial-Bold", fontSize=11.5, leading=15,
                    textColor=BLUE, spaceBefore=8, spaceAfter=4, keepWithNext=1)
title = ParagraphStyle("title", parent=body, fontName="Arial-Bold", fontSize=24, leading=28,
                       textColor=BLUE, spaceAfter=2)
subtitle = ParagraphStyle("subtitle", parent=body, fontSize=12, leading=16,
                          textColor=colors.HexColor("#555555"))
bullet = ParagraphStyle("bullet", parent=body, leftIndent=12, bulletIndent=2, spaceAfter=2)
code = ParagraphStyle("code", fontName="Consolas", fontSize=8.5, leading=11, leftIndent=6,
                      backColor=GREY, borderPadding=(4, 4, 4, 4), spaceBefore=3, spaceAfter=8)


def P(text, style=body):
    return Paragraph(text, style)


def bullets(items):
    return [Paragraph(t, bullet, bulletText="•") for t in items]


def steps(items):
    return [Paragraph(t, bullet, bulletText=f"{i}.") for i, t in enumerate(items, 1)]


def C(text):
    return Preformatted(text, code)


def table(rows, widths, header=True):
    data = [[Paragraph(c, cellb if header and r == 0 else cell) for c in row]
            for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#B0B8C8")),
             ("VALIGN", (0, 0), (-1, -1), "TOP"),
             ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    if header:
        style += [("BACKGROUND", (0, 0), (-1, 0), BLUE)]
    for r in range(1 if header else 0, len(rows)):
        if r % 2 == 0:
            style.append(("BACKGROUND", (0, r), (-1, r), LIGHT))
    t.setStyle(TableStyle(style))
    return t


def icon_image():
    img = make_icon.art_cloud()
    w, h = img.size
    path = os.path.join(tempfile.gettempdir(), "amidrive_icon_doc.png")
    img.resize((w * 4, h * 4), make_icon.Image.NEAREST).save(path)       # transparent PNG
    return Image(path, width=26 * mm, height=26 * mm * h / w)


def content():
    s = []
    head = Table([[icon_image(), [P("AmiDrive", title),
                                  P("Klient OneDrive dla AmigaOS 3.x – opis działania, stan na A1200 "
                                    "i instrukcja dla nowego użytkownika", subtitle)]]],
                 colWidths=[30 * mm, 140 * mm])
    head.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "MIDDLE")]))
    s += [head, Spacer(1, 4),
          P("Wersja programu 0.4.2, dokument z 18.10.2026.", small), Spacer(1, 6)]

    # ---------------------------------------------------------------- 1
    s.append(P("1. Czym jest AmiDrive", h1))
    s.append(P("AmiDrive to natywny program dla Amigi, który synchronizuje <b>jeden wybrany folder</b> "
               "z OneDrive z katalogiem na dysku Amigi. Łączy się bezpośrednio z Microsoft Graph "
               "przez internet (HTTPS, TLS 1.2/1.3) – bez PC, Raspberry Pi ani rclone. Pliki wrzucone "
               "do tego folderu na telefonie lub PC pojawiają się na Amidze, a pliki zmienione na "
               "Amidze trafiają do chmury."))
    s += bullets([
        "Amiga: AmigaOS 3.x, procesor 68020 lub szybszy, cel 2–4 MB Fast RAM.",
        "Sieć: dowolny stos TCP/IP z <font name='Consolas'>bsdsocket.library</font> (Roadshow, "
        "AmiTCP, Miami); szyfrowanie przez AmiSSL 5.",
        "Logowanie kodem urządzenia: hasło do konta Microsoft nigdy nie trafia do Amigi.",
        "Praca w tle: sprawdzanie zmian co kilka minut, tylko różnice od ostatniego razu.",
    ])

    # ---------------------------------------------------------------- 2
    s.append(P("2. Jak działa", h1))
    s.append(P("Jedno <b>przejście synchronizacji</b> wygląda tak:", body))
    s += steps([
        "<b>Token.</b> Program czyta token z <font name='Consolas'>ENVARC:AmiDrive/token</font>; gdy "
        "wygasł (ok. 1 h), odświeża go sam w Microsoft i zapisuje nowy.",
        "<b>Zmiany w chmurze.</b> Zapytanie <font name='Consolas'>/delta</font> o synchronizowany folder. "
        "Pierwszy raz OneDrive zwraca całą zawartość, później tylko różnice od zapamiętanego punktu "
        "(deltaLink). Odpowiedzi są czytane stronami po 20 elementów, żeby zmieścić się w małej pamięci.",
        "<b>Pobieranie na Amigę.</b> Nowe i zmienione pliki są pobierane strumieniowo do pliku "
        "tymczasowego w tym samym katalogu i dopiero potem podmieniane; data pliku jak w chmurze. "
        "Przeniesienia i zmiany nazw wykonywane są lokalnie, bez ponownego pobierania. Usunięte "
        "w chmurze – usuwane na Amidze.",
        "<b>Zmiany na Amidze.</b> Program przegląda katalog lokalny i porównuje go z bazą stanu "
        "(rozmiar i data każdego pliku po ostatniej synchronizacji). Nowe pliki i foldery wysyła, "
        "zmienione wysyła jako nowe wersje, przeniesione przenosi w chmurze, usunięte usuwa "
        "(trafiają do kosza OneDrive).",
        "<b>Stan.</b> Baza stanu (<font name='Consolas'>.AmiDrive/state</font>) zapisywana jest "
        "bezpiecznie: do pliku tymczasowego, potem podmiana. Nowy punkt delta zapamiętywany jest tylko "
        "po bezbłędnym przejściu – nieudany plik wraca w następnym przejściu.",
    ])
    s.append(P("Konflikty i bezpieczeństwo danych", h2))
    s.append(table([
        ["Sytuacja", "Co robi AmiDrive"],
        ["Plik zmieniony i na Amidze, i w chmurze",
         "Wersja z chmury trafia pod właściwą nazwę, wersja z Amigi zostaje jako "
         "<i>nazwa-Amiga.ext</i> i jest wysyłana jako osobny plik. Nic nie ginie."],
        ["Plik usunięty na Amidze, a zmieniony w chmurze", "Plik wraca z chmury."],
        ["Plik usunięty w chmurze, a zmieniony na Amidze", "Plik na Amidze zostaje."],
        ["Brak większości plików lokalnie (np. odłączony dysk)",
         "Gdy brakuje ponad 10 elementów i ponad połowy – nic nie jest usuwane w chmurze, "
         "w dzienniku jest ostrzeżenie."],
        ["Przerwanie (Ctrl-C, reset, brak sieci)",
         "Stan zostaje spójny; następne przejście kończy przerwaną pracę."],
    ], [62 * mm, 108 * mm]))
    s.append(Spacer(1, 6))
    s.append(P("Nazwy plików", h2))
    s += bullets([
        "OneDrive używa UTF-8, Amiga Latin-1 albo Latin-2 (<font name='Consolas'>CHARSET=LATIN2</font> "
        "dla polskich liter). Znaki spoza strony kodowej zamieniane są na „_”.",
        "Nazwy dłuższe niż pozwala dysk (OFS/FFS: 30 znaków) są skracane do postaci "
        "<i>początek~hhhhhh.ext</i>; pełna nazwa z chmury pamiętana jest w bazie stanu.",
        "Bity ochrony i komentarze plików Amigi nie są przenoszone (OneDrive ich nie zna).",
        "Pomijane: notesy OneNote, elementy udostępnione z cudzego dysku, pliki od 4 GB, "
        "katalog <font name='Consolas'>.AmiDrive</font>.",
    ])

    # ---------------------------------------------------------------- 3
    s.append(P("3. Jak AmiDrive działa teraz na A1200", h1))
    s.append(P("Stan z 18.10.2026 (A1200, 68060, AmigaOS 3.2.2, Roadshow, AmiSSL 5.24, "
               "OneDrive for Business):"))
    s.append(table([
        ["Co", "Gdzie / wartość"],
        ["Program i ikona", "<font name='Consolas'>Work:AmiDrive/AmiDrive</font>, "
                            "<font name='Consolas'>Work:AmiDrive/AmiDrive.info</font>"],
        ["Autostart", "<font name='Consolas'>SystemSD:WBStartup/AmiDrive.info</font> (ikona projektu: "
                      "narzędzie domyślne <font name='Consolas'>Work:AmiDrive/AmiDrive</font>, "
                      "DONOTWAIT) i krótki plik tekstowy <font name='Consolas'>SystemSD:WBStartup/AmiDrive</font>"],
        ["Synchronizowany folder w chmurze", "<font name='Consolas'>/Retro/AmigaDrive</font>"],
        ["Katalog na Amidze", "<font name='Consolas'>Work:OneDrive</font>"],
        ["Ustawienia", "<font name='Consolas'>ENVARC:AmiDrive/AmiDrive.prefs</font>: LOCAL, REMOTE, "
                       "CHARSET=LATIN2, INTERVAL=3, PAUSE=YES (wysyłanie włączone – domyślnie)"],
        ["Token logowania (tajny)", "<font name='Consolas'>ENVARC:AmiDrive/token</font>"],
        ["Stan i dziennik", "<font name='Consolas'>Work:OneDrive/.AmiDrive/state</font>, "
                            "<font name='Consolas'>Work:OneDrive/.AmiDrive/AmiDrive.log</font>"],
        ["Strefa czasowa", "<font name='Consolas'>TZ=CET-1CEST,M3.5.0,M10.5.0/3</font> (czas letni)"],
    ], [52 * mm, 118 * mm]))
    s.append(Spacer(1, 6))
    s.append(P("Sposób uruchomienia", h2))
    s.append(P("AmiDrive <b>startuje sam razem z Workbenchem</b>: w "
               "<font name='Consolas'>SystemSD:WBStartup</font> leży ikona projektu, która uruchamia "
               "program z <font name='Consolas'>Work:AmiDrive</font> (bez drugiej kopii programu – "
               "nowa wersja w <font name='Consolas'>Work:AmiDrive</font> od razu działa też przy "
               "autostarcie). Sieć jest już wtedy uruchomiona (<font name='Consolas'>S:User-Startup</font>). "
               "Tool Types ikony programu są wyłączone (w nawiasach), więc ustawienia pochodzą z pliku "
               "prefs. Program pracuje <b>w tle, bez okna</b>. Ustawione jest "
               "<font name='Consolas'>PAUSE=YES</font>: po starcie AmiDrive <b>czeka w pauzie</b> "
               "(„Paused”, jak po <i>Stop</i>) i nic nie sprawdza ani nie wysyła, aż do <i>Sync now</i> "
               "w oknie lub w menu <i>Narzędzia</i>; od tej chwili co 3 minuty robi przejście i zapisuje "
               "je do dziennika. Okno pojawia się tylko przy błędzie albo logowaniu."))
    s.append(P("Ikona „OneDrive” na pulpicie", h2))
    s.append(P("Dopóki AmiDrive pracuje w tle, na pulpicie Workbencha jest ikona <b>OneDrive</b> "
               "(chmurka; w trakcie synchronizacji z pomarańczowymi okrągłymi strzałkami):"))
    s += bullets([
        "<b>dwuklik</b> otwiera synchronizowany katalog <font name='Consolas'>Work:OneDrive</font> "
        "(AmigaOS 3.5 lub nowszy; od 3.9/3.2 jako lista wszystkich plików według nazw) "
        "i <b>okno AmiDrive</b> z przyciskami <i>Sync now</i>, "
        "<i>Stop</i> (pauza synchronizacji) i <i>Quit</i> (koniec programu) – od razu, także w trakcie synchronizacji; wtedy okno "
        "pokazuje postęp: pasek z procentami, bieżący plik, prędkość i szacowany czas do końca "
        "(Workbench 3.2 nie ma menu ikon pod prawym przyciskiem);",
        "<b>pliki i szuflady upuszczone na ikonę</b> są kopiowane do tego katalogu i od razu "
        "wysyłane do chmury;",
        "menu <i>Narzędzia</i>: <b>AmiDrive: status</b> (okno postępu albo wynik ostatniego "
        "sprawdzenia i godzina następnego), <b>AmiDrive: synchronise now</b> (przejście teraz) i "
        "<b>AmiDrive: quit</b> (koniec pracy w tle, ikona znika).",
    ])
    s.append(P("Workbench sam wybiera miejsce ikony (pierwsze wolne w kolumnie ikon – może być "
               "zasłonięte oknem). Bez ikony: <font name='Consolas'>APPICON=NO</font>."))
    s.append(P("Obsługa na co dzień", h2))
    s.append(table([
        ["Czynność", "Jak"],
        ["Uruchomić w tle", "Dwuklik na ikonie <i>AmiDrive</i> albo w Shellu: "
                            "<font name='Consolas'>Run &gt;NIL: Work:AmiDrive/AmiDrive</font>"],
        ["Zakończyć", "Przycisk <i>Quit</i>, menu <i>Narzędzia → AmiDrive: quit</i>, ponowny dwuklik na ikonie programu → "
                      "<i>Stop</i>, albo w Shellu <font name='Consolas'>Work:AmiDrive/AmiDrive STOP</font>"],
        ["Jedno przejście teraz", "Menu <i>Narzędzia → AmiDrive: synchronise now</i> (gdy działa w tle), "
                                  "albo <font name='Consolas'>Work:AmiDrive/AmiDrive ONCE</font>"],
        ["Co się działo", "<font name='Consolas'>Type Work:OneDrive/.AmiDrive/AmiDrive.log</font>"],
        ["Stan konta i ustawień", "<font name='Consolas'>Work:AmiDrive/AmiDrive INFO</font>"],
        ["Wyłączyć autostart", "Usunąć <font name='Consolas'>SystemSD:WBStartup/AmiDrive</font> i "
                               "<font name='Consolas'>AmiDrive.info</font> z tego katalogu"],
    ], [42 * mm, 128 * mm]))
    s.append(Spacer(1, 4))
    s.append(P("Naraz może działać tylko jedna synchronizacja: druga kopia uruchomiona z Shella "
               "kończy się komunikatem „already running”, a z ikony pyta, czy zatrzymać działającą.",
               body))

    # ---------------------------------------------------------------- 4
    s.append(P("4. Co musi zrobić inny użytkownik (własny OneDrive)", h1))
    s.append(P("Wymagania", h2))
    s += bullets([
        "AmigaOS 3.x (sprawdzone na 3.2.2), procesor 68020 lub nowszy, kilka MB Fast RAM.",
        "Działający stos TCP/IP z <font name='Consolas'>bsdsocket.library</font> v4 (zalecany Roadshow) "
        "i połączenie z internetem (karta sieciowa, WiFi PCMCIA, Plipbox…).",
        "AmiSSL 5.24 lub nowszy (z certyfikatami w <font name='Consolas'>AmiSSL:Certs</font>).",
        "Konto Microsoft z OneDrive: prywatne (outlook.com, hotmail, live) albo firmowe/szkolne "
        "(Microsoft 365).",
    ])
    s.append(P("Krok po kroku", h2))
    s += steps([
        "<b>Zegar i strefa czasowa.</b> Ustaw poprawną datę i godzinę (bez tego TLS odrzuci certyfikaty "
        "serwerów). Dla Polski strefa z czasem letnim:",
    ])
    s.append(C("SetEnv SAVE TZ CET-1CEST,M3.5.0,M10.5.0/3"))
    s += [Paragraph(t, bullet, bulletText=f"{i}.") for i, t in enumerate([
        "<b>Instalacja.</b> Skopiuj <font name='Consolas'>AmiDrive</font> i "
        "<font name='Consolas'>AmiDrive.info</font> do dowolnego katalogu, np. "
        "<font name='Consolas'>Work:AmiDrive</font>.",
        "<b>Folder w OneDrive.</b> W przeglądarce lub aplikacji OneDrive utwórz folder, który ma być "
        "synchronizowany, np. <font name='Consolas'>Amiga</font>. Musi istnieć – AmiDrive go nie "
        "tworzy (komunikat „OneDrive folder … not found”). Najlepiej zacząć od folderu z niewielką "
        "liczbą plików.",
        "<b>Logowanie.</b> W Shellu <font name='Consolas'>AmiDrive LOGIN</font> (albo po prostu kliknij "
        "ikonę – bez logowania program sam otworzy okno). Na telefonie lub PC otwórz "
        "<font name='Consolas'>https://login.microsoft.com/device</font>, wpisz kod z ekranu Amigi, "
        "zaloguj się i zgódź na dostęp aplikacji <i>AmiDrive</i> do plików. Token zapisze się "
        "w <font name='Consolas'>ENVARC:AmiDrive/token</font>.",
        "<b>Ustawienia</b> (zapamiętywane):",
    ], 2)]
    s.append(C("AmiDrive LOCAL=Work:OneDrive REMOTE=/Amiga CHARSET=LATIN2 ONCE"))
    s.append(P("To pierwsze przejście pobierze zawartość folderu. Zamiast Shella można wpisać te same "
               "opcje w Tool Types ikony (<i>Ikony → Informacje…</i>, usunąć nawiasy).", body))
    s += [Paragraph(t, bullet, bulletText=f"{i}.") for i, t in enumerate([
        "<b>Praca w tle i autostart.</b> Kliknij ikonę albo <font name='Consolas'>Run &gt;NIL: AmiDrive</font>. "
        "Autostart: skopiuj program z ikoną do <font name='Consolas'>SYS:WBStartup</font> (ikona ma "
        "DONOTWAIT) albo – jak na A1200 – połóż tam ikonę projektu z narzędziem domyślnym "
        "wskazującym program (<font name='Consolas'>tools/make_icon.py --project</font>). "
        "Żeby po starcie AmiDrive czekał na <i>Sync now</i>: <font name='Consolas'>AmiDrive PAUSE=YES</font>.",
    ], 6)]
    s.append(P("Instalator (<i>Install-AmiDrive</i>) pyta o to wszystko sam: o szufladę programu, "
               "katalog na pliki, folder w OneDrive, stronę kodową, częstotliwość, kierunek, ikonę "
               "„OneDrive”, zegar, strefę czasową, autostart i o to, co robić po uruchomieniu "
               "(synchronizować od razu albo czekać w pauzie do <i>Sync now</i>).", body))
    s.append(P("Kilka kont OneDrive", h2))
    s.append(P("AmiDrive obsługuje do 3 kont (np. firmowe i prywatne); synchronizowane jest jedno "
               "naraz. W aktywnym oknie AmiDrive prawy przycisk pokazuje menu <b>Account</b>: lista "
               "kont (zaznaczone aktywne, „(start)” przy koncie startowym), <i>Start with this account</i> "
               "(od tego konta AmiDrive zawsze zaczyna; bez tego – od ostatnio używanego), "
               "<i>Sign in...</i> i <i>Sign out</i>. Wybranie pustego konta "
               "pokazuje kod logowania w oknie, a potem pyta o szufladę na Amidze i folder w tym "
               "OneDrive. Każde konto ma własną szufladę, folder i token (konto 1: "
               "<font name='Consolas'>AmiDrive.prefs</font> i <font name='Consolas'>token</font>, "
               "konta 2–3: <font name='Consolas'>Account2.prefs</font>, <font name='Consolas'>token2</font> …). "
               "W Shellu: <font name='Consolas'>AmiDrive ACCOUNT=2</font>."))
    s.append(P("Konto prywatne a firmowe – rejestracja aplikacji", h2))
    s.append(P("AmiDrive loguje się przez aplikację zarejestrowaną w Microsoft Entra ID (identyfikator "
               "wbudowany w program: <font name='Consolas'>0474e444-3266-446b-b49c-d47a01cadf56</font>, "
               "dla kont prywatnych i firmowych dowolnej organizacji)."))
    s += bullets([
        "<b>Konto prywatne</b> – wystarczy zalogować się i zaakceptować zgodę.",
        "<b>Konto firmowe innej organizacji</b> – administrator może blokować aplikacje spoza "
        "organizacji. Wtedy logowanie kończy się prośbą o zgodę administratora. Rozwiązania: "
        "administrator zatwierdza aplikację <i>AmiDrive</i> (Enterprise applications → Grant admin "
        "consent) albo organizacja rejestruje własną aplikację, jak niżej.",
    ])
    s.append(KeepTogether([
        P("Własna rejestracja aplikacji (portal Entra lub Azure Portal):", body),
        *steps([
            "<i>App registrations → New registration</i>; nazwa np. AmiDrive; typ kont: tylko ta "
            "organizacja albo dowolne organizacje i konta prywatne.",
            "<i>Authentication → Advanced settings → Allow public client flows: Yes</i> "
            "(logowanie kodem urządzenia, bez sekretu).",
            "<i>API permissions → Microsoft Graph → Delegated</i>: <b>Files.ReadWrite</b> i "
            "<b>offline_access</b> (opcjonalnie zgoda administratora dla całej organizacji).",
            "Skopiuj <i>Application (client) ID</i> i <i>Directory (tenant) ID</i>, potem na Amidze:",
        ]),
        C("AmiDrive CLIENTID=<client-id> TENANT=<tenant-id albo organizations>\nAmiDrive LOGIN"),
    ]))
    s.append(P("Szybkość", h2))
    s.append(P("Na Amidze szybkość ogranicza szyfrowanie połączeń, nie sieć. Pomiar na A1200 "
               "(68060/50, Ethernet karty Warp), ten sam plik 5 MB:"))
    s.append(table([
        ["Połączenie", "Szybkość"],
        ["bez szyfrowania (http)", "1590 KB/s"],
        ["TLS 1.3, AES-256-GCM", "215 KB/s"],
        ["TLS 1.3, ChaCha20-Poly1305", "337 KB/s"],
        ["OneDrive, pełna synchronizacja 20 MB (AES-128 – wybór Microsoftu)", "192 KB/s (108 s; z AES-256: 130 s)"],
    ], [110 * mm, 60 * mm]))
    s.append(Spacer(1, 4))
    s.append(P("AmiDrive proponuje szyfry najszybsze na 68k (ChaCha20, potem AES-128), wszystkie "
               "w pełni bezpieczne. Własny pomiar: <font name='Consolas'>AmiDrive TEST "
               "URL=https://…</font>."))
    s.append(P("Bezpieczeństwo", h2))
    s += bullets([
        "Plik <font name='Consolas'>ENVARC:AmiDrive/token</font> daje dostęp do plików w OneDrive – "
        "nie udostępniaj go i nie kopiuj na inne komputery. <font name='Consolas'>AmiDrive LOGOUT</font> "
        "usuwa go z Amigi; dostęp można też odebrać na koncie Microsoft (Aplikacje i usługi).",
        "Certyfikaty serwerów są zawsze sprawdzane; tokeny nigdy nie trafiają do dziennika.",
    ])

    # ---------------------------------------------------------------- 5
    s.append(P("5. Opcje", h1))
    s.append(P("Te same w Shellu i w Tool Types ikony. Pierwszeństwo: Shell, potem Tool Types, potem "
               "plik ustawień. Zapisywane są tylko opcje podane w Shellu."))
    s.append(table([
        ["Opcja", "Znaczenie"],
        ["LOCAL=katalog", "katalog na Amidze, np. Work:OneDrive (tworzony, gdy go nie ma)"],
        ["REMOTE=/folder", "folder w OneDrive, np. /Amiga (musi istnieć)"],
        ["CHARSET=LATIN1|LATIN2", "strona kodowa nazw na Amidze (domyślnie LATIN1)"],
        ["INTERVAL=min", "co ile minut sprawdzać zmiany w tle (domyślnie 3)"],
        ["UPLOAD=YES|NO", "NO – tylko pobieranie z chmury, bez wysyłania (domyślnie YES)"],
        ["STATE=katalog", "inne miejsce bazy stanu i dziennika (domyślnie LOCAL/.AmiDrive)"],
        ["ONCE", "jedno przejście i koniec"],
        ["WINDOW", "(z ikony) okno z bieżącym postępem"],
        ["APPICON=YES|NO", "ikona „OneDrive” na pulpicie w pracy w tle (domyślnie YES)"],
        ["PAUSE=YES|NO", "YES – start w pauzie („Paused”), pierwsze sprawdzenie dopiero po "
                         "<i>Sync now</i> (domyślnie NO); pyta o to też instalator"],
        ["CLOCK=ASK|AUTO|NO", "zły zegar Amigi: okno z pytaniem (domyślnie) / ustaw sam przy starcie i "
                              "raz na dobę / nie sprawdzaj"],
        ["SETCLOCK", "ustawia zegar Amigi (i zegar z baterią) czasem z internetu"],
        ["STOP", "zatrzymuje AmiDrive działający w tle"],
        ["LOGIN / LOGOUT / INFO", "logowanie, wylogowanie, stan konta i ustawień"],
        ["TEST", "sprawdza zegar, strefę, DNS, TLS i połączenie z Microsoft Graph"],
        ["CLIENTID= / TENANT=", "własna rejestracja aplikacji (rozdział 4)"],
        ["ACCOUNT=1|2|3", "aktywne konto OneDrive (zapamiętywane); w oknie menu Account"],
        ["START=0|1|2|3", "konto na start (0: ostatnio używane); w menu Start with this account"],
    ], [48 * mm, 122 * mm]))

    # ---------------------------------------------------------------- 6
    s.append(P("6. Problemy i ograniczenia", h1))
    s.append(table([
        ["Komunikat / objaw", "Przyczyna i rada"],
        ["Not signed in", "brak tokenu: AmiDrive LOGIN"],
        ["Sign in again", "token wygasł lub odebrano dostęp (np. zmiana hasła): AmiDrive LOGIN"],
        ["OneDrive folder … not found", "zła ścieżka REMOTE albo folder nie istnieje"],
        ["certificate rejected, check the Amiga clock",
         "zły zegar Amigi: AmiDrive pokaże okno z czasem z internetu i przyciskiem <i>Set clock</i> "
         "(CLOCK=ASK) albo ustawi go sam (CLOCK=AUTO); ręcznie: AmiDrive SETCLOCK"],
        ["certificate rejected (inne)", "brak lub stare certyfikaty AmiSSL – zaktualizować AmiSSL; AmiDrive TEST"],
        ["already running", "inna kopia już synchronizuje: AmiDrive STOP"],
        ["Plik z dopiskiem -Amiga", "konflikt – obie wersje zachowane, wybierz właściwą"],
    ], [52 * mm, 118 * mm]))
    s.append(Spacer(1, 6))
    s += bullets([
        "Przerwane pobieranie lub wysyłanie dużego pliku zaczyna się od nowa (bez wznawiania).",
        "Zmiana nazwy folderu na Amidze: w chmurze powstaje nowy folder, pliki są do niego "
        "przenoszone (bez wysyłania), stary jest usuwany.",
        "Jeden synchronizowany folder; pliki do 4 GB; czas letni tylko ze zmiennej TZ.",
        "Proces AmiDrive zawieszony po awarii blokuje nowe uruchomienie aż do restartu Amigi.",
    ])
    return s


def content_en():
    """the same document in English"""
    s = []
    head = Table([[icon_image(), [P("AmiDrive", title),
                                  P("OneDrive client for AmigaOS 3.x – how it works, how it runs on "
                                    "the A1200 and a guide for a new user", subtitle)]]],
                 colWidths=[30 * mm, 140 * mm])
    head.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "MIDDLE")]))
    s += [head, Spacer(1, 4),
          P("Program version 0.4.2, document of 18 October 2026.", small), Spacer(1, 6)]

    # ---------------------------------------------------------------- 1
    s.append(P("1. What AmiDrive is", h1))
    s.append(P("AmiDrive is a native Amiga program that synchronises <b>one chosen OneDrive folder</b> "
               "with a directory on the Amiga's disk. It connects directly to Microsoft Graph over "
               "the Internet (HTTPS, TLS 1.2/1.3) – no PC, Raspberry Pi or rclone needed. Files put "
               "into that folder on a phone or PC appear on the Amiga, and files changed on the "
               "Amiga go to the cloud."))
    s += bullets([
        "Amiga: AmigaOS 3.x, 68020 or faster, aims at 2–4 MB of Fast RAM.",
        "Network: any TCP/IP stack with <font name='Consolas'>bsdsocket.library</font> (Roadshow, "
        "AmiTCP, Miami); encryption by AmiSSL 5.",
        "Sign-in with a device code: the Microsoft account password never reaches the Amiga.",
        "Runs in the background: checks for changes every few minutes, only the differences "
        "since the last time.",
    ])

    # ---------------------------------------------------------------- 2
    s.append(P("2. How it works", h1))
    s.append(P("One <b>synchronisation pass</b> goes like this:", body))
    s += steps([
        "<b>Token.</b> The program reads the token from <font name='Consolas'>ENVARC:AmiDrive/token</font>; "
        "when it has expired (after about 1 h) it renews it at Microsoft and saves the new one.",
        "<b>Changes in the cloud.</b> A <font name='Consolas'>/delta</font> request for the synchronised "
        "folder. The first time OneDrive returns the whole content, later only the differences since "
        "the remembered point (deltaLink). Answers are read in pages of 20 items to fit in little memory.",
        "<b>Download to the Amiga.</b> New and changed files are streamed into a temporary file in "
        "the same directory and only then put in place; the file date is the one from the cloud. "
        "Moves and renames are done locally, without downloading again. Deleted in the cloud – "
        "deleted on the Amiga.",
        "<b>Changes on the Amiga.</b> The program scans the local directory and compares it with "
        "the state database (size and date of each file after the last synchronisation). New files "
        "and folders are uploaded, changed files are uploaded as new versions, moved ones are moved "
        "in the cloud, deleted ones are deleted (they go to the OneDrive recycle bin).",
        "<b>State.</b> The state database (<font name='Consolas'>.AmiDrive/state</font>) is saved "
        "safely: into a temporary file, then swapped in. The new delta point is remembered only "
        "after a pass without errors – a file that failed comes back in the next pass.",
    ])
    s.append(P("Conflicts and data safety", h2))
    s.append(table([
        ["Situation", "What AmiDrive does"],
        ["File changed both on the Amiga and in the cloud",
         "The cloud version gets the proper name, the Amiga version stays as "
         "<i>name-Amiga.ext</i> and is uploaded as a separate file. Nothing is lost."],
        ["File deleted on the Amiga but changed in the cloud", "The file comes back from the cloud."],
        ["File deleted in the cloud but changed on the Amiga", "The file on the Amiga stays."],
        ["Most files missing locally (e.g. a disk not mounted)",
         "When more than 10 items and more than half are missing, nothing is deleted in the "
         "cloud and the log has a warning."],
        ["Interruption (Ctrl-C, reset, no network)",
         "The state stays consistent; the next pass finishes the interrupted work."],
    ], [62 * mm, 108 * mm]))
    s.append(Spacer(1, 6))
    s.append(P("File names", h2))
    s += bullets([
        "OneDrive uses UTF-8, the Amiga Latin-1 or Latin-2 (<font name='Consolas'>CHARSET=LATIN2</font> "
        "for Central European letters). Characters outside the character set become “_”.",
        "Names longer than the disk allows (OFS/FFS: 30 characters) are shortened to "
        "<i>start~hhhhhh.ext</i>; the full cloud name is kept in the state database.",
        "Amiga protection bits and file comments are not transferred (OneDrive does not know them).",
        "Skipped: OneNote notebooks, items shared from someone else's drive, files of 4 GB and more, "
        "the <font name='Consolas'>.AmiDrive</font> directory.",
    ])

    # ---------------------------------------------------------------- 3
    s.append(P("3. How AmiDrive runs on the author's A1200", h1))
    s.append(P("State of 18 October 2026 (A1200, 68060, AmigaOS 3.2.2, Roadshow, AmiSSL 5.24, "
               "OneDrive for Business):"))
    s.append(table([
        ["What", "Where / value"],
        ["Program and icon", "<font name='Consolas'>Work:AmiDrive/AmiDrive</font>, "
                             "<font name='Consolas'>Work:AmiDrive/AmiDrive.info</font>"],
        ["Autostart", "<font name='Consolas'>SystemSD:WBStartup/AmiDrive.info</font> (project icon: "
                      "default tool <font name='Consolas'>Work:AmiDrive/AmiDrive</font>, "
                      "DONOTWAIT) and a short text file <font name='Consolas'>SystemSD:WBStartup/AmiDrive</font>"],
        ["Synchronised cloud folder", "<font name='Consolas'>/Retro/AmigaDrive</font>"],
        ["Directory on the Amiga", "<font name='Consolas'>Work:OneDrive</font>"],
        ["Settings", "<font name='Consolas'>ENVARC:AmiDrive/AmiDrive.prefs</font>: LOCAL, REMOTE, "
                     "CHARSET=LATIN2, INTERVAL=3, PAUSE=YES (upload on – the default)"],
        ["Sign-in token (secret)", "<font name='Consolas'>ENVARC:AmiDrive/token</font>"],
        ["State and log", "<font name='Consolas'>Work:OneDrive/.AmiDrive/state</font>, "
                          "<font name='Consolas'>Work:OneDrive/.AmiDrive/AmiDrive.log</font>"],
        ["Time zone", "<font name='Consolas'>TZ=CET-1CEST,M3.5.0,M10.5.0/3</font> (with summer time)"],
    ], [52 * mm, 118 * mm]))
    s.append(Spacer(1, 6))
    s.append(P("How it is started", h2))
    s.append(P("AmiDrive <b>starts by itself with the Workbench</b>: "
               "<font name='Consolas'>SystemSD:WBStartup</font> holds a project icon that starts "
               "the program in <font name='Consolas'>Work:AmiDrive</font> (no second copy of the "
               "program – a new version in <font name='Consolas'>Work:AmiDrive</font> is used by the "
               "autostart at once). The network is already up then (<font name='Consolas'>S:User-Startup</font>). "
               "The Tool Types of the program icon are switched off (in brackets), so the settings come "
               "from the prefs file. The program runs <b>in the background, without a window</b>. "
               "<font name='Consolas'>PAUSE=YES</font> is set: after the start AmiDrive <b>waits "
               "paused</b> (“Paused”, as after <i>Stop</i>) and checks and sends nothing until "
               "<i>Sync now</i> in the window or the <i>Tools</i> menu; from then on it makes a pass "
               "every 3 minutes and writes it to the log. A window appears only for an error or a sign-in."))
    s.append(P("The “OneDrive” icon on the Workbench", h2))
    s.append(P("While AmiDrive runs in the background, the Workbench shows a <b>OneDrive</b> icon "
               "(a cloud; with orange round arrows during a synchronisation):"))
    s += bullets([
        "<b>double-click</b> opens the synchronised directory <font name='Consolas'>Work:OneDrive</font> "
        "(AmigaOS 3.5 or newer; from 3.9/3.2 as a list of all files by name) "
        "and the <b>AmiDrive window</b> with the buttons <i>Sync now</i>, "
        "<i>Stop</i> (pauses the synchronisation) and <i>Quit</i> (ends the program) – at once, also "
        "during a synchronisation; then the window shows the progress: a bar with percent, the current "
        "file, the speed and the estimated time left (Workbench 3.2 has no icon menu on the right button);",
        "<b>files and drawers dropped on the icon</b> are copied into that directory and uploaded "
        "to the cloud at once;",
        "<i>Tools</i> menu: <b>AmiDrive: status</b> (the progress window, or the result of the last "
        "check and the time of the next one), <b>AmiDrive: synchronise now</b> (a pass now) and "
        "<b>AmiDrive: quit</b> (ends the background work, the icon goes away).",
    ])
    s.append(P("The Workbench chooses the place of the icon (the first free one in the icon column – "
               "it may be hidden by a window). Without the icon: <font name='Consolas'>APPICON=NO</font>."))
    s.append(P("Everyday use", h2))
    s.append(table([
        ["Task", "How"],
        ["Run in the background", "Double-click the <i>AmiDrive</i> icon, or in a Shell: "
                                  "<font name='Consolas'>Run &gt;NIL: Work:AmiDrive/AmiDrive</font>"],
        ["End it", "The <i>Quit</i> button, menu <i>Tools → AmiDrive: quit</i>, double-click the program "
                   "icon again → <i>Stop</i>, or in a Shell <font name='Consolas'>Work:AmiDrive/AmiDrive STOP</font>"],
        ["One pass now", "Menu <i>Tools → AmiDrive: synchronise now</i> (while it runs in the background), "
                         "or <font name='Consolas'>Work:AmiDrive/AmiDrive ONCE</font>"],
        ["What happened", "<font name='Consolas'>Type Work:OneDrive/.AmiDrive/AmiDrive.log</font>"],
        ["Account and settings", "<font name='Consolas'>Work:AmiDrive/AmiDrive INFO</font>"],
        ["Switch off the autostart", "Delete <font name='Consolas'>SystemSD:WBStartup/AmiDrive</font> and "
                                     "<font name='Consolas'>AmiDrive.info</font> from that directory"],
    ], [42 * mm, 128 * mm]))
    s.append(Spacer(1, 4))
    s.append(P("Only one synchronisation can run at a time: a second copy started from a Shell ends "
               "with “already running”, one started from the icon asks whether to stop the running one.",
               body))

    # ---------------------------------------------------------------- 4
    s.append(P("4. What another user has to do (their own OneDrive)", h1))
    s.append(P("Requirements", h2))
    s += bullets([
        "AmigaOS 3.x (tested on 3.2.2), 68020 or newer, a few MB of Fast RAM.",
        "A working TCP/IP stack with <font name='Consolas'>bsdsocket.library</font> v4 (Roadshow "
        "recommended) and an Internet connection (network card, PCMCIA WiFi, Plipbox…).",
        "AmiSSL 5.24 or newer (with the certificates in <font name='Consolas'>AmiSSL:Certs</font>).",
        "A Microsoft account with OneDrive: personal (outlook.com, hotmail, live) or work/school "
        "(Microsoft 365).",
    ])
    s.append(P("Step by step", h2))
    s += steps([
        "<b>Clock and time zone.</b> Set the correct date and time (without it TLS rejects the "
        "servers' certificates). For example Central Europe with summer time:",
    ])
    s.append(C("SetEnv SAVE TZ CET-1CEST,M3.5.0,M10.5.0/3"))
    s += [Paragraph(t, bullet, bulletText=f"{i}.") for i, t in enumerate([
        "<b>Installation.</b> Copy <font name='Consolas'>AmiDrive</font> and "
        "<font name='Consolas'>AmiDrive.info</font> to any directory, e.g. "
        "<font name='Consolas'>Work:AmiDrive</font>.",
        "<b>Folder in OneDrive.</b> In the browser or the OneDrive app create the folder to be "
        "synchronised, e.g. <font name='Consolas'>Amiga</font>. It must exist – AmiDrive does not "
        "create it (message “OneDrive folder … not found”). Best start with a folder with few files.",
        "<b>Sign-in.</b> In a Shell <font name='Consolas'>AmiDrive LOGIN</font> (or just click the "
        "icon – without a sign-in the program opens a window by itself). On a phone or PC open "
        "<font name='Consolas'>https://login.microsoft.com/device</font>, enter the code from the "
        "Amiga screen, sign in and allow the <i>AmiDrive</i> app to access your files. The token is "
        "saved in <font name='Consolas'>ENVARC:AmiDrive/token</font>.",
        "<b>Settings</b> (remembered):",
    ], 2)]
    s.append(C("AmiDrive LOCAL=Work:OneDrive REMOTE=/Amiga CHARSET=LATIN2 ONCE"))
    s.append(P("This first pass downloads the content of the folder. Instead of the Shell the same "
               "options can be put in the Tool Types of the icon (<i>Icons → Information…</i>, "
               "remove the brackets).", body))
    s += [Paragraph(t, bullet, bulletText=f"{i}.") for i, t in enumerate([
        "<b>Background and autostart.</b> Click the icon or <font name='Consolas'>Run &gt;NIL: AmiDrive</font>. "
        "Autostart: copy the program with its icon to <font name='Consolas'>SYS:WBStartup</font> (the "
        "icon has DONOTWAIT) or – as on the A1200 – put a project icon there with the default tool "
        "pointing at the program (<font name='Consolas'>tools/make_icon.py --project</font>). "
        "To make AmiDrive wait for <i>Sync now</i> after the start: <font name='Consolas'>AmiDrive PAUSE=YES</font>.",
    ], 6)]
    s.append(P("The installer (<i>Install-AmiDrive</i>) asks for all of this by itself: the program "
               "drawer, the directory for the files, the OneDrive folder, the character set, how often "
               "to check, the direction, the “OneDrive” icon, the clock, the time zone, the autostart "
               "and what to do after the start (synchronise at once or wait paused until "
               "<i>Sync now</i>).", body))
    s.append(P("Several OneDrive accounts", h2))
    s.append(P("AmiDrive handles up to 3 accounts (e.g. work and personal); one is synchronised at "
               "a time. In the active AmiDrive window the right mouse button shows the <b>Account</b> "
               "menu: the accounts (the active one ticked, “(start)” at the start account), "
               "<i>Start with this account</i> (AmiDrive always starts with it; without it – with the "
               "last one used), <i>Sign in...</i> and <i>Sign out</i>. Choosing an empty account shows "
               "the sign-in code in the window, then asks for the drawer on the Amiga and the folder in "
               "that OneDrive. Each account has its own drawer, folder and token (account 1: "
               "<font name='Consolas'>AmiDrive.prefs</font> and <font name='Consolas'>token</font>, "
               "accounts 2–3: <font name='Consolas'>Account2.prefs</font>, <font name='Consolas'>token2</font> …). "
               "In a Shell: <font name='Consolas'>AmiDrive ACCOUNT=2</font>."))
    s.append(P("Personal and work accounts – the app registration", h2))
    s.append(P("AmiDrive signs in through an app registered in Microsoft Entra ID (the ID built into "
               "the program: <font name='Consolas'>0474e444-3266-446b-b49c-d47a01cadf56</font>, "
               "for personal accounts and work accounts of any organisation)."))
    s += bullets([
        "<b>Personal account</b> – just sign in and accept the consent.",
        "<b>Work account of another organisation</b> – the administrator may block apps from outside "
        "the organisation. Then the sign-in ends with a request for admin consent. Solutions: the "
        "administrator approves the <i>AmiDrive</i> app (Enterprise applications → Grant admin "
        "consent), or the organisation registers its own app as below.",
    ])
    s.append(KeepTogether([
        P("Your own app registration (Entra portal or Azure Portal):", body),
        *steps([
            "<i>App registrations → New registration</i>; name e.g. AmiDrive; account types: this "
            "organisation only, or any organisation and personal accounts.",
            "<i>Authentication → Advanced settings → Allow public client flows: Yes</i> "
            "(device code sign-in, no secret).",
            "<i>API permissions → Microsoft Graph → Delegated</i>: <b>Files.ReadWrite</b> and "
            "<b>offline_access</b> (optionally admin consent for the whole organisation).",
            "Copy the <i>Application (client) ID</i> and the <i>Directory (tenant) ID</i>, then on the Amiga:",
        ]),
        C("AmiDrive CLIENTID=<client-id> TENANT=<tenant-id or organizations>\nAmiDrive LOGIN"),
    ]))
    s.append(P("Speed", h2))
    s.append(P("On the Amiga the speed is limited by the encryption, not by the network. Measured on "
               "the A1200 (68060/50, Ethernet of the Warp card), the same 5 MB file:"))
    s.append(table([
        ["Connection", "Speed"],
        ["no encryption (http)", "1590 KB/s"],
        ["TLS 1.3, AES-256-GCM", "215 KB/s"],
        ["TLS 1.3, ChaCha20-Poly1305", "337 KB/s"],
        ["OneDrive, full synchronisation of 20 MB (AES-128 – Microsoft's choice)",
         "192 KB/s (108 s; with AES-256: 130 s)"],
    ], [110 * mm, 60 * mm]))
    s.append(Spacer(1, 4))
    s.append(P("AmiDrive offers the ciphers fastest on the 68k (ChaCha20, then AES-128), all fully "
               "secure. Your own measurement: <font name='Consolas'>AmiDrive TEST URL=https://…</font>."))
    s.append(P("Security", h2))
    s += bullets([
        "The file <font name='Consolas'>ENVARC:AmiDrive/token</font> gives access to the files in "
        "OneDrive – do not share it or copy it to other computers. <font name='Consolas'>AmiDrive LOGOUT</font> "
        "deletes it from the Amiga; access can also be revoked in the Microsoft account (Apps and services).",
        "Server certificates are always checked; tokens never go into the log.",
    ])

    # ---------------------------------------------------------------- 5
    s.append(P("5. Options", h1))
    s.append(P("The same in a Shell and in the Tool Types of the icon. Precedence: Shell, then Tool "
               "Types, then the settings file. Only options given in a Shell are saved."))
    s.append(table([
        ["Option", "Meaning"],
        ["LOCAL=directory", "directory on the Amiga, e.g. Work:OneDrive (created when missing)"],
        ["REMOTE=/folder", "folder in OneDrive, e.g. /Amiga (must exist)"],
        ["CHARSET=LATIN1|LATIN2", "character set of names on the Amiga (default LATIN1)"],
        ["INTERVAL=min", "minutes between checks in the background (default 3)"],
        ["UPLOAD=YES|NO", "NO – download from the cloud only, no upload (default YES)"],
        ["STATE=directory", "another place for the state database and the log (default LOCAL/.AmiDrive)"],
        ["ONCE", "one pass, then end"],
        ["WINDOW", "(from the icon) a window with the progress"],
        ["APPICON=YES|NO", "the “OneDrive” icon on the Workbench while running in the background (default YES)"],
        ["PAUSE=YES|NO", "YES – start paused (“Paused”), the first check only after "
                         "<i>Sync now</i> (default NO); the installer asks about it too"],
        ["CLOCK=ASK|AUTO|NO", "wrong Amiga clock: a window asking (default) / set it at the start and "
                              "once a day / do not check"],
        ["SETCLOCK", "sets the Amiga clock (and the battery clock) from the Internet"],
        ["STOP", "stops the AmiDrive running in the background"],
        ["LOGIN / LOGOUT / INFO", "sign in, sign out, account and settings"],
        ["TEST", "checks the clock, time zone, DNS, TLS and the connection to Microsoft Graph"],
        ["CLIENTID= / TENANT=", "your own app registration (chapter 4)"],
        ["ACCOUNT=1|2|3", "the active OneDrive account (remembered); in the window: menu Account"],
        ["START=0|1|2|3", "the account to start with (0: the last one used); menu Start with this account"],
    ], [48 * mm, 122 * mm]))

    # ---------------------------------------------------------------- 6
    s.append(P("6. Problems and limits", h1))
    s.append(table([
        ["Message / symptom", "Cause and advice"],
        ["Not signed in", "no token: AmiDrive LOGIN"],
        ["Sign in again", "the token expired or access was revoked (e.g. password change): AmiDrive LOGIN"],
        ["OneDrive folder … not found", "wrong REMOTE path or the folder does not exist"],
        ["certificate rejected, check the Amiga clock",
         "wrong Amiga clock: AmiDrive shows a window with the time from the Internet and a "
         "<i>Set clock</i> button (CLOCK=ASK) or sets it by itself (CLOCK=AUTO); by hand: AmiDrive SETCLOCK"],
        ["certificate rejected (other)", "missing or old AmiSSL certificates – update AmiSSL; AmiDrive TEST"],
        ["already running", "another copy is synchronising: AmiDrive STOP"],
        ["A file ending in -Amiga", "a conflict – both versions are kept, choose the right one"],
    ], [52 * mm, 118 * mm]))
    s.append(Spacer(1, 6))
    s += bullets([
        "An interrupted download or upload of a large file starts again from the beginning (no resume).",
        "Renaming a folder on the Amiga: a new folder is created in the cloud, the files are moved "
        "into it (without uploading), the old one is deleted.",
        "One synchronised folder; files up to 4 GB; summer time only from the TZ variable.",
        "An AmiDrive process hung after a crash blocks a new start until the Amiga is restarted.",
    ])
    return s


# language -> file, footer, page word, metadata, content
DOCS = {
    "pl": ("AmiDrive-opis.pdf", "AmiDrive 0.4.2 – opis działania i instalacji", "strona",
           "AmiDrive – opis", "Klient OneDrive dla AmigaOS", content),
    "en": ("AmiDrive-description.pdf", "AmiDrive 0.4.2 – how it works and how to install it", "page",
           "AmiDrive – description", "OneDrive client for AmigaOS", content_en),
}


def build(lang, out):
    _, foot, page, doc_title, subject, make = DOCS[lang]

    def footer_lang(canvas, doc):
        canvas.saveState()
        canvas.setFont("Arial", 8)
        canvas.setFillColor(colors.HexColor("#777777"))
        canvas.drawString(18 * mm, 10 * mm, foot)
        canvas.drawRightString(A4[0] - 18 * mm, 10 * mm, f"{page} {doc.page}")
        canvas.restoreState()

    doc = SimpleDocTemplate(out, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm,
                            topMargin=16 * mm, bottomMargin=18 * mm, title=doc_title,
                            author="AmiDrive", subject=subject)
    doc.build(make(), onFirstPage=footer_lang, onLaterPages=footer_lang)
    print(out)


def main():
    langs = [a for a in sys.argv[1:] if a in DOCS] or list(DOCS)
    for lang in langs:
        build(lang, os.path.join(ROOT, "docs", DOCS[lang][0]))


if __name__ == "__main__":
    main()
