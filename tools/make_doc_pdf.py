#!/usr/bin/env python3
"""Build docs/AmiDrive-opis.pdf: how AmiDrive works, how it runs on the A1200 now and
what another user has to do to use it with their own OneDrive (in Polish).

  python tools/make_doc_pdf.py [OUT.pdf]

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


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Arial", 8)
    canvas.setFillColor(colors.HexColor("#777777"))
    canvas.drawString(18 * mm, 10 * mm, "AmiDrive 0.3.2 – opis działania i instalacji")
    canvas.drawRightString(A4[0] - 18 * mm, 10 * mm, f"strona {doc.page}")
    canvas.restoreState()


def content():
    s = []
    head = Table([[icon_image(), [P("AmiDrive", title),
                                  P("Klient OneDrive dla AmigaOS 3.x – opis działania, stan na A1200 "
                                    "i instrukcja dla nowego użytkownika", subtitle)]]],
                 colWidths=[30 * mm, 140 * mm])
    head.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "MIDDLE")]))
    s += [head, Spacer(1, 4),
          P("Wersja programu 0.3.2, dokument z 14.10.2026.", small), Spacer(1, 6)]

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
    s.append(P("Stan z 14.10.2026 (A1200, 68060, AmigaOS 3.2.2, Roadshow, AmiSSL 5.24, "
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
                       "CHARSET=LATIN2, INTERVAL=3 (wysyłanie włączone – domyślnie)"],
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
               "prefs. Program pracuje <b>w tle, bez okna</b>: co 3 minuty robi przejście i zapisuje "
               "je do dziennika. Okno pojawia się tylko przy błędzie albo logowaniu."))
    s.append(P("Ikona „OneDrive” na pulpicie", h2))
    s.append(P("Dopóki AmiDrive pracuje w tle, na pulpicie Workbencha jest ikona <b>OneDrive</b> "
               "(chmurka):"))
    s += bullets([
        "<b>dwuklik</b> otwiera synchronizowany katalog <font name='Consolas'>Work:OneDrive</font> "
        "(AmigaOS 3.5 lub nowszy) – od razu, także w trakcie synchronizacji; wtedy pokazuje się "
        "też <b>okno postępu</b>: pasek z procentami, bieżący plik, prędkość i szacowany czas do końca;",
        "<b>pliki i szuflady upuszczone na ikonę</b> są kopiowane do tego katalogu i od razu "
        "wysyłane do chmury;",
        "menu <i>Narzędzia</i>: <b>AmiDrive: status</b> (okno postępu albo wynik ostatniego "
        "sprawdzenia i godzina następnego), <b>AmiDrive: synchronise now</b> (przejście teraz) i "
        "<b>AmiDrive: stop</b> (koniec pracy w tle, ikona znika).",
    ])
    s.append(P("Workbench sam wybiera miejsce ikony (pierwsze wolne w kolumnie ikon – może być "
               "zasłonięte oknem). Bez ikony: <font name='Consolas'>APPICON=NO</font>."))
    s.append(P("Obsługa na co dzień", h2))
    s.append(table([
        ["Czynność", "Jak"],
        ["Uruchomić w tle", "Dwuklik na ikonie <i>AmiDrive</i> albo w Shellu: "
                            "<font name='Consolas'>Run &gt;NIL: Work:AmiDrive/AmiDrive</font>"],
        ["Zatrzymać", "Menu <i>Narzędzia → AmiDrive: stop</i>, ponowny dwuklik na ikonie programu → "
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
        "wskazującym program (<font name='Consolas'>tools/make_icon.py --project</font>).",
    ], 6)]
    s.append(P("Konto prywatne a firmowe – rejestracja aplikacji", h2))
    s.append(P("AmiDrive loguje się przez aplikację zarejestrowaną w Microsoft Entra ID (identyfikator "
               "wbudowany w program: <font name='Consolas'>0474e444-3266-446b-b49c-d47a01cadf56</font>, "
               "dla kont prywatnych i firmowych dowolnej organizacji)."))
    s += bullets([
        "<b>Konto prywatne</b> – wystarczy zalogować się i zaakceptować zgodę. (Do tej pory "
        "AmiDrive był sprawdzany z kontem firmowym; z kontem prywatnym jeszcze nie.)",
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
        ["CLOCK=ASK|AUTO|NO", "zły zegar Amigi: okno z pytaniem (domyślnie) / ustaw sam przy starcie i "
                              "raz na dobę / nie sprawdzaj"],
        ["SETCLOCK", "ustawia zegar Amigi (i zegar z baterią) czasem z internetu"],
        ["STOP", "zatrzymuje AmiDrive działający w tle"],
        ["LOGIN / LOGOUT / INFO", "logowanie, wylogowanie, stan konta i ustawień"],
        ["TEST", "sprawdza zegar, strefę, DNS, TLS i połączenie z Microsoft Graph"],
        ["CLIENTID= / TENANT=", "własna rejestracja aplikacji (rozdział 4)"],
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


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "AmiDrive-opis.pdf")
    doc = SimpleDocTemplate(out, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm,
                            topMargin=16 * mm, bottomMargin=18 * mm, title="AmiDrive – opis",
                            author="AmiDrive", subject="Klient OneDrive dla AmigaOS")
    doc.build(content(), onFirstPage=footer, onLaterPages=footer)
    print(out)


if __name__ == "__main__":
    main()
