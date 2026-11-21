#!/usr/bin/env python3
"""Rogue Galaxy for the Steam Deck: the installer.

One file, to run once in desktop mode (double-click, "Execute"). One page
asks for your disc image, where to put the game and the options; then it
downloads the game's sources, builds them on the Deck (in a container, 10 to 30 minutes), and adds the game
to Steam. Afterwards it is not needed any more: the game starts on its own
home screen, which updates it, in game mode.

Needs what SteamOS has: python3 with GTK 4 (PyGObject), curl, git, distrobox.

The window speaks English, French, German, Spanish or Italian: Steam's language to begin with
(else the locale), and a list in the top right corner changes it. --lang CODE
or RG_LANG=CODE forces one.

Saves: a PCSX2 memory card with Rogue Galaxy saves (USA) is looked for in PCSX2's
usual folders (Flatpak, AppImage, EmuDeck, RetroDECK, the SD card) and offered; --saves
CARD (a .ps2 file or a folder card) names one. They are copied into disc/mc0, never over a save already there.

For tests: --iso FILE, --dir FOLDER, --zip ARCHIVE (a release's source
archive instead of the download), --no-steam, --start (skip the pages),
--demo (the install page with a made-up build, nothing installed),
--shots FOLDER (a PNG of the window; --tour also shows the European option
and the install page). RG_UPDATE_REPO=owner/name and
RG_UPDATE_API=URL point the release lookup elsewhere; RG_SETUP_REF=BRANCH is
the branch cloned when the repository has no release yet.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import random
import re
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
import zlib
from pathlib import Path

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
gi.require_version("Gsk", "4.0")
gi.require_version("Graphene", "1.0")
from gi.repository import Gdk, Gio, GLib, Graphene, Gsk, Gtk, Pango  # noqa: E402

REPO = os.environ.get("RG_UPDATE_REPO", "touyoo0/deck-galaxy")
API = os.environ.get("RG_UPDATE_API", f"https://api.github.com/repos/{REPO}/releases?per_page=10")
REF = os.environ.get("RG_SETUP_REF", "steamdeck-only")
NEED_GB = 24  # the extracted disc, the generated code, the build
ISO_BYTES = 6_967_721_984  # Redump's image of the USA release

CSS = b"""
window { background: #04061a; color: #f2f3fc; }
.brand { font-size: 30px; font-weight: 800; letter-spacing: 7px; color: #f4cb52;
         text-shadow: 0 0 18px rgba(244,203,82,0.55), 0 3px 0 #6b3d00; }
.tagline { font-size: 12px; font-weight: 700; letter-spacing: 4px; color: #ff8c7a; }
.title { font-size: 24px; font-weight: 700; color: #f2f3fc; }
.subtitle { font-size: 14px; color: #b4bde0; }
.muted { color: #9ea8cc; }
.pill { color: #d4daf2; background: rgba(8,12,42,0.72); border-radius: 10px; padding: 6px 16px; }
.section { font-size: 17px; font-weight: 700; color: #f2f3fc; }
.badge { background-image: linear-gradient(#ffdf7a, #f0a82a); color: #1a1500; font-weight: 800;
         border-radius: 15px; min-width: 30px; min-height: 30px;
         box-shadow: 0 0 14px rgba(244,203,82,0.6); }
.step { font-size: 12px; font-weight: 700; color: #6f7aa8; padding: 2px 10px; border-radius: 10px; }
.step-on { color: #1a1500; background-image: linear-gradient(#ffdf7a, #f0a82a); }
.step-done { color: #f4cb52; }
.recap { font-size: 15px; color: #e6e9fa; }
.good { color: #70de8e; }
.warn { color: #f6b640; }
.bad { color: #ff7b72; }
.card.invalid { border-color: #ff5a4f; border-width: 2px; }
.invalid-text, .invalid-text label { color: #ff7b72; }
.card { background: rgba(8,12,42,0.74); border: 1px solid rgba(244,203,82,0.30); border-radius: 14px;
        padding: 6px 12px; box-shadow: 0 10px 34px rgba(0,0,0,0.5); }
.tip-title { font-size: 12px; font-weight: 700; letter-spacing: 3px; color: #f4cb52; }
.tip { font-size: 22px; color: #e6e9fa; }
.tip-head { font-size: 34px; font-weight: 800; color: #f4cb52; text-shadow: 0 2px 0 #6b3d00; }
.tip-count { font-size: 12px; color: #9ea8cc; }
button { background: rgba(28,40,110,0.65); color: #f2f3fc; border: 1px solid rgba(127,212,255,0.4);
         border-radius: 9px; padding: 5px 14px; box-shadow: none; background-image: none; }
button:hover { background: rgba(48,66,160,0.8); border-color: #7fd4ff; }
button:disabled { color: #5f6890; background: rgba(16,21,47,0.7); border-color: rgba(127,212,255,0.12); }
button.suggested-action { background-image: linear-gradient(#ffdf7a, #f0a82a); color: #1a1500; border-color: #ffe9a0;
                          font-size: 16px; font-weight: 800; padding: 9px 40px; border-radius: 12px;
                          box-shadow: 0 0 22px rgba(244,203,82,0.45); }
button.suggested-action:hover { background-image: linear-gradient(#ffeaa0, #ffbe3a); }
button.suggested-action:disabled { background-image: none; background: rgba(74,66,36,0.75); color: #8a8160;
                                   border-color: transparent; box-shadow: none; }
progressbar trough { background: rgba(26,32,70,0.9); min-height: 14px; border-radius: 8px;
                     border: 1px solid rgba(127,212,255,0.3); }
progressbar progress { background-image: linear-gradient(90deg, #ffb13b, #ffe37a); min-height: 14px;
                       border-radius: 8px; box-shadow: 0 0 14px rgba(244,203,82,0.7); }
checkbutton { color: #f2f3fc; }
check, radio { background: rgba(16,22,58,0.9); border: 1px solid rgba(127,212,255,0.5); }
check:checked, radio:checked { background: #f4cb52; color: #1a1500; border-color: #ffe9a0; }
expander title { color: #b4bde0; }
dropdown > button { padding: 4px 10px; }
popover > contents { background: #10163a; color: #f2f3fc; border: 1px solid rgba(127,212,255,0.4);
                     border-radius: 10px; }
popover listview, popover row { background: transparent; color: #f2f3fc; }
popover row:hover, popover row:selected { background: rgba(48,66,160,0.8); }
textview, textview text { background: #04071a; color: #9ea8cc; font-family: monospace; font-size: 12px; }
"""


# ---------------------------------------------------------------- languages
# Every text of the window, in this order: English, French, German, Spanish, Italian. English is the
# fallback. The window's language is the Deck's own (see detect_language) and can be changed with the
# list in the corner. Logs (the "Details" and build/setup.log) stay in English, for reports.

LANGUAGES = (("en", "English"), ("fr", "Français"), ("de", "Deutsch"), ("es", "Español"), ("it", "Italiano"))
CODES = [code for code, _ in LANGUAGES]
STEAM_LANGUAGES = {"english": "en", "french": "fr", "german": "de", "spanish": "es", "latam": "es",
                   "italian": "it"}
_current = ["en"]

STRINGS = {
    "window_title": ("Rogue Galaxy: install", "Rogue Galaxy : installation", "Rogue Galaxy: Installation",
                     "Rogue Galaxy: instalación", "Rogue Galaxy: installazione"),
    "tagline": ("FOR THE STEAM DECK   ·   INSTALLER", "POUR LE STEAM DECK   ·   INSTALLATEUR",
                "FÜR DAS STEAM DECK   ·   INSTALLATION", "PARA LA STEAM DECK   ·   INSTALADOR",
                "PER LO STEAM DECK   ·   INSTALLATORE"),
    "header_sub": ("Built on this Deck from your own disc image; nothing leaves it.",
                   "Compilé sur ce Steam Deck à partir de votre propre image disque ; rien n'en sort.",
                   "Wird auf diesem Steam Deck aus deinem eigenen Disc-Image erstellt; nichts verlässt das Gerät.",
                   "Se compila en esta Steam Deck a partir de tu propia imagen de disco; nada sale de ella.",
                   "Compilato su questo Steam Deck dalla tua immagine del disco; nulla esce dal dispositivo."),
    "language": ("Language", "Langue", "Sprache", "Idioma", "Lingua"),
    # 1. the disc image
    "sec_disc": ("Your disc image", "Votre image disque", "Dein Disc-Image", "Tu imagen de disco",
                 "La tua immagine del disco"),
    "disc_hint": ("USA, SCUS-97490, uncompressed .iso", "USA, SCUS-97490, .iso non compressé",
                  "USA, SCUS-97490, unkomprimierte .iso", "USA, SCUS-97490, .iso sin comprimir",
                  "USA, SCUS-97490, .iso non compresso"),
    "choose_iso": ("Choose the .iso...", "Choisir le .iso...", "Die .iso wählen...", "Elegir el .iso...",
                   "Scegli il .iso..."),
    "iso_dialog": ("Your Rogue Galaxy (USA) disc image", "Votre image disque de Rogue Galaxy (USA)",
                   "Dein Rogue-Galaxy-Disc-Image (USA)", "Tu imagen de disco de Rogue Galaxy (USA)",
                   "La tua immagine del disco di Rogue Galaxy (USA)"),
    "iso_extracted": ("Already extracted in the folder: no image needed.",
                      "Déjà extrait dans le dossier : aucune image nécessaire.",
                      "Bereits im Ordner entpackt: kein Image nötig.",
                      "Ya extraído en la carpeta: no hace falta ninguna imagen.",
                      "Già estratto nella cartella: nessuna immagine necessaria."),
    "iso_ok": ("This looks like a disc image. It is checked in full while installing.",
               "Cela ressemble à une image disque. Elle est vérifiée en entier pendant l'installation.",
               "Das sieht nach einem Disc-Image aus. Es wird bei der Installation vollständig geprüft.",
               "Parece una imagen de disco. Se comprueba por completo durante la instalación.",
               "Sembra un'immagine del disco. Viene verificata per intero durante l'installazione."),
    "iso_missing": ("That file does not exist.", "Ce fichier n'existe pas.", "Diese Datei existiert nicht.",
                    "Ese archivo no existe.", "Quel file non esiste."),
    "iso_ext": ("It must be an uncompressed .iso (turn a .chd or .cso back into an .iso first).",
                "Ce doit être un .iso non compressé (reconvertissez d'abord un .chd ou un .cso en .iso).",
                "Es muss eine unkomprimierte .iso sein (wandle eine .chd oder .cso zuerst wieder in eine .iso um).",
                "Debe ser un .iso sin comprimir (convierte antes un .chd o .cso de nuevo a .iso).",
                "Deve essere un .iso non compresso (riconverti prima un .chd o .cso in .iso)."),
    "iso_small": ("That is too small for a PlayStation 2 DVD image.",
                  "C'est trop petit pour une image de DVD PlayStation 2.",
                  "Das ist zu klein für ein PlayStation-2-DVD-Image.",
                  "Es demasiado pequeño para una imagen de DVD de PlayStation 2.",
                  "È troppo piccolo per un'immagine di DVD PlayStation 2."),
    "iso_notdisc": ("That is not a disc image.", "Ce n'est pas une image disque.", "Das ist kein Disc-Image.",
                    "Eso no es una imagen de disco.", "Non è un'immagine del disco."),
    # 2. where
    "sec_where": ("Where to install", "Où installer", "Installationsort", "Dónde instalar", "Dove installare"),
    "where_hint": ("The game, its saves and its build go in one folder.",
                   "Le jeu, ses sauvegardes et sa compilation vont dans un seul dossier.",
                   "Spiel, Spielstände und Build liegen in einem Ordner.",
                   "El juego, sus partidas guardadas y su compilación van en una sola carpeta.",
                   "Il gioco, i salvataggi e la compilazione stanno in un'unica cartella."),
    "other_folder": ("Another folder...", "Un autre dossier...", "Anderer Ordner...", "Otra carpeta...",
                     "Un'altra cartella..."),
    "folder_dialog": ("Install the game in", "Installer le jeu dans", "Das Spiel installieren in",
                      "Instalar el juego en", "Installa il gioco in"),
    "storage_internal": ("Internal storage", "Stockage interne", "Interner Speicher", "Almacenamiento interno",
                         "Memoria interna"),
    "storage_sd": ("SD card", "Carte SD", "SD-Karte", "Tarjeta SD", "Scheda SD"),
    "where_row": ("{label}: {path}   ({free} GB free)", "{label} : {path}   ({free} Go libres)",
                  "{label}: {path}   ({free} GB frei)", "{label}: {path}   ({free} GB libres)",
                  "{label}: {path}   ({free} GB liberi)"),
    "no_room": ("(not enough room)", "(pas assez de place)", "(nicht genug Platz)", "(no hay espacio suficiente)",
                "(spazio insufficiente)"),
    "where_note": ("{path}   ({free} GB free)", "{path}   ({free} Go libres)", "{path}   ({free} GB frei)",
                   "{path}   ({free} GB libres)", "{path}   ({free} GB liberi)"),
    "where_note_short": ("{path}   ({free} GB free, not enough)", "{path}   ({free} Go libres, insuffisant)",
                         "{path}   ({free} GB frei, nicht genug)", "{path}   ({free} GB libres, insuficiente)",
                         "{path}   ({free} GB liberi, insufficiente)"),
    # 3. options
    "sec_options": ("Options", "Options", "Optionen", "Opciones", "Opzioni"),
    "options_hint": ("All can wait: the home screen can add them later.",
                     "Tout peut attendre : l'écran d'accueil permet de les ajouter plus tard.",
                     "Alles kann warten: Der Startbildschirm kann sie später hinzufügen.",
                     "Todo puede esperar: la pantalla de inicio permite añadirlas más tarde.",
                     "Tutto può aspettare: la schermata iniziale potrà aggiungerle più tardi."),
    "steam_check": ("Add Rogue Galaxy to Steam (play it from game mode)",
                    "Ajouter Rogue Galaxy à Steam (jouer depuis le mode jeu)",
                    "Rogue Galaxy zu Steam hinzufügen (im Spielmodus spielen)",
                    "Añadir Rogue Galaxy a Steam (jugar desde el modo juego)",
                    "Aggiungi Rogue Galaxy a Steam (gioca dalla modalità gioco)"),
    "european_check": ("Add the European disc (SCES-54552) for FR, DE, ES and IT texts",
                       "Ajouter le disque européen (SCES-54552) pour les textes FR, DE, ES et IT",
                       "Die europäische Disc (SCES-54552) für FR-, DE-, ES- und IT-Texte hinzufügen",
                       "Añadir el disco europeo (SCES-54552) para los textos FR, DE, ES e IT",
                       "Aggiungi il disco europeo (SCES-54552) per i testi FR, DE, ES e IT"),
    "european_choose": ("Choose the European .iso...", "Choisir le .iso européen...",
                        "Die europäische .iso wählen...", "Elegir el .iso europeo...", "Scegli il .iso europeo..."),
    "european_dialog": ("Rogue Galaxy European disc", "Disque européen de Rogue Galaxy",
                        "Europäische Rogue-Galaxy-Disc", "Disco europeo de Rogue Galaxy",
                        "Disco europeo di Rogue Galaxy"),
    "european_none": ("No image chosen", "Aucune image choisie", "Kein Image gewählt", "Ninguna imagen elegida",
                      "Nessuna immagine scelta"),
    "saves_check": ("Import my PCSX2 save of Rogue Galaxy",
                    "Importer ma sauvegarde PCSX2 de Rogue Galaxy",
                    "Meinen PCSX2-Spielstand von Rogue Galaxy importieren",
                    "Importar mi partida guardada de PCSX2 de Rogue Galaxy",
                    "Importa il mio salvataggio PCSX2 di Rogue Galaxy"),
    "saves_found": ("Found: {name} ({n} saves), in {where}",
                    "Trouvé : {name} ({n} sauvegardes), dans {where}",
                    "Gefunden: {name} ({n} Spielstände), in {where}",
                    "Encontrado: {name} ({n} partidas), en {where}",
                    "Trovato: {name} ({n} salvataggi), in {where}"),
    "saves_found_one": ("Found: {name} (1 save), in {where}", "Trouvé : {name} (1 sauvegarde), dans {where}",
                        "Gefunden: {name} (1 Spielstand), in {where}", "Encontrado: {name} (1 partida), en {where}",
                        "Trovato: {name} (1 salvataggio), in {where}"),
    "saves_none": ("No save found on this Deck: choose your memory card yourself.",
                   "Aucune sauvegarde trouvée sur ce Steam Deck : choisissez vous-même votre carte mémoire.",
                   "Auf diesem Steam Deck wurde kein Spielstand gefunden: Wähle deine Memory Card selbst.",
                   "No se encontró ninguna partida en esta Deck: elige tú mismo tu tarjeta de memoria.",
                   "Nessun salvataggio trovato su questo Deck: scegli tu la memory card."),
    "saves_bad": ("There is no Rogue Galaxy save (USA, SCUS-97490) in that.",
                  "Il n'y a aucune sauvegarde de Rogue Galaxy (USA, SCUS-97490) là-dedans.",
                  "Darin ist kein Rogue-Galaxy-Spielstand (USA, SCUS-97490).",
                  "No hay ninguna partida de Rogue Galaxy (USA, SCUS-97490) ahí.",
                  "Lì dentro non c'è nessun salvataggio di Rogue Galaxy (USA, SCUS-97490)."),
    "saves_file": ("Choose a card file (.ps2)...", "Choisir un fichier de carte (.ps2)...",
                   "Kartendatei wählen (.ps2)...", "Elegir un archivo de tarjeta (.ps2)...",
                   "Scegli un file di card (.ps2)..."),
    "saves_folder": ("Choose a card folder...", "Choisir un dossier de carte...", "Kartenordner wählen...",
                     "Elegir una carpeta de tarjeta...", "Scegli una cartella di card..."),
    "saves_file_dialog": ("PCSX2 memory card (.ps2)", "Carte mémoire PCSX2 (.ps2)", "PCSX2-Memory-Card (.ps2)",
                          "Tarjeta de memoria de PCSX2 (.ps2)", "Memory card PCSX2 (.ps2)"),
    "saves_folder_dialog": ("PCSX2 memory card folder", "Dossier de carte mémoire PCSX2",
                            "PCSX2-Memory-Card-Ordner", "Carpeta de tarjeta de memoria de PCSX2",
                            "Cartella della memory card PCSX2"),
    "ra_check": ("Earn the game's 130 achievements (RetroAchievements account)",
                 "Obtenir les 130 succès du jeu (compte RetroAchievements)",
                 "Die 130 Erfolge des Spiels freischalten (RetroAchievements-Konto)",
                 "Conseguir los 130 logros del juego (cuenta de RetroAchievements)",
                 "Ottieni i 130 obiettivi del gioco (account RetroAchievements)"),
    "ra_user": ("RetroAchievements user", "Utilisateur RetroAchievements", "RetroAchievements-Benutzer",
                "Usuario de RetroAchievements", "Utente RetroAchievements"),
    "ra_password": ("Password", "Mot de passe", "Passwort", "Contraseña", "Password"),
    "ra_signin": ("Sign in", "Se connecter", "Anmelden", "Iniciar sesión", "Accedi"),
    "ra_done": ("Signed in already: it stays as it is.", "Déjà connecté : rien ne change.",
                "Bereits angemeldet: bleibt unverändert.", "Ya has iniciado sesión: se queda como está.",
                "Accesso già effettuato: resta com'è."),
    "ra_hint": ("The password is sent once to retroachievements.org and never kept.",
                "Le mot de passe est envoyé une seule fois à retroachievements.org et jamais conservé.",
                "Das Passwort wird einmal an retroachievements.org gesendet und nie gespeichert.",
                "La contraseña se envía una sola vez a retroachievements.org y nunca se guarda.",
                "La password viene inviata una sola volta a retroachievements.org e non viene mai conservata."),
    "ra_need": ("Type the user and the password first.", "Saisissez d'abord l'utilisateur et le mot de passe.",
                "Gib zuerst Benutzer und Passwort ein.", "Escribe primero el usuario y la contraseña.",
                "Inserisci prima l'utente e la password."),
    "ra_signing": ("Signing in...", "Connexion...", "Anmeldung...", "Iniciando sesión...", "Accesso in corso..."),
    "ra_unreachable": ("RetroAchievements could not be reached. Check the connection, or sign in later (see docs).",
                       "RetroAchievements est injoignable. Vérifiez la connexion, ou connectez-vous plus tard (voir la doc).",
                       "RetroAchievements ist nicht erreichbar. Prüfe die Verbindung oder melde dich später an (siehe Doku).",
                       "No se pudo contactar con RetroAchievements. Comprueba la conexión o inicia sesión más tarde (consulta la documentación).",
                       "RetroAchievements non è raggiungibile. Controlla la connessione o accedi più tardi (vedi la documentazione)."),
    "ra_refused": ("Sign-in refused.", "Connexion refusée.", "Anmeldung abgelehnt.",
                   "Inicio de sesión rechazado.", "Accesso rifiutato."),
    "ra_signed_as": ("Signed in as {user}.", "Connecté en tant que {user}.", "Angemeldet als {user}.",
                     "Sesión iniciada como {user}.", "Accesso effettuato come {user}."),
    "ra_auto": ("The game turns RetroAchievements on by itself.", "Le jeu active RetroAchievements tout seul.",
                "Das Spiel aktiviert RetroAchievements automatisch.",
                "El juego activa RetroAchievements por sí solo.", "Il gioco attiva RetroAchievements da solo."),
    # the install page
    "install": ("Install", "Installer", "Installieren", "Instalar", "Installa"),
    "installing": ("Installing", "Installation en cours", "Installation läuft", "Instalando",
                   "Installazione in corso"),
    "install_sub": ("Keep the Deck plugged in. You can leave this window open and do something else.",
                    "Gardez le Steam Deck branché. Vous pouvez laisser cette fenêtre ouverte et faire autre chose.",
                    "Lass das Steam Deck angeschlossen. Du kannst dieses Fenster offen lassen und etwas anderes tun.",
                    "Mantén la Steam Deck enchufada. Puedes dejar esta ventana abierta y hacer otra cosa.",
                    "Tieni lo Steam Deck collegato all'alimentazione. Puoi lasciare aperta questa finestra e fare altro."),
    "starting": ("Starting", "Démarrage", "Start", "Iniciando", "Avvio"),
    "details": ("Details", "Détails", "Details", "Detalles", "Dettagli"),
    "close": ("Close", "Fermer", "Schließen", "Cerrar", "Chiudi"),
    "stopped": ("The install stopped", "L'installation s'est arrêtée", "Die Installation wurde abgebrochen",
                "La instalación se detuvo", "L'installazione si è interrotta"),
    "done": ("Done", "Terminé", "Fertig", "Hecho", "Fatto"),
    "done_sub": ("Rogue Galaxy is installed.", "Rogue Galaxy est installé.", "Rogue Galaxy ist installiert.",
                 "Rogue Galaxy está instalado.", "Rogue Galaxy è installato."),
    "done_steam": ("It is in your Steam library, under Non-Steam. Switch to game mode and play.",
                   "Il est dans votre bibliothèque Steam, sous « Non-Steam ». Passez en mode jeu et jouez.",
                   "Es befindet sich in deiner Steam-Bibliothek unter „Nicht-Steam“. Wechsle in den Spielmodus und spiele.",
                   "Está en tu biblioteca de Steam, en «No Steam». Cambia al modo juego y a jugar.",
                   "È nella tua libreria Steam, sotto «Non Steam». Passa alla modalità gioco e gioca."),
    "done_nosteam": ("Add rogue-galaxy.sh to Steam as a non-Steam game, then switch to game mode and play.",
                     "Ajoutez rogue-galaxy.sh à Steam comme jeu non-Steam, puis passez en mode jeu et jouez.",
                     "Füge rogue-galaxy.sh zu Steam als Nicht-Steam-Spiel hinzu, wechsle dann in den Spielmodus und spiele.",
                     "Añade rogue-galaxy.sh a Steam como juego que no es de Steam, luego cambia al modo juego y a jugar.",
                     "Aggiungi rogue-galaxy.sh a Steam come gioco non Steam, poi passa alla modalità gioco e gioca."),
    "done_tail": ("The game opens on a home screen with Play and Update; this installer is not needed any more.",
                  "Le jeu s'ouvre sur un écran d'accueil avec Jouer et Mettre à jour ; cet installateur n'est plus nécessaire.",
                  "Das Spiel startet mit einem Startbildschirm mit Spielen und Aktualisieren; dieser Installer wird nicht mehr gebraucht.",
                  "El juego se abre en una pantalla de inicio con Jugar y Actualizar; este instalador ya no hace falta.",
                  "Il gioco si apre su una schermata iniziale con Gioca e Aggiorna; questo installatore non serve più."),
    # cancelling
    "cancel_q": ("Cancel the installation?", "Annuler l'installation ?", "Installation abbrechen?",
                 "¿Cancelar la instalación?", "Annullare l'installazione?"),
    "cancel_detail": ("The build will be stopped. {kept}", "La compilation sera arrêtée. {kept}",
                      "Der Build wird gestoppt. {kept}", "Se detendrá la compilación. {kept}",
                      "La compilazione verrà interrotta. {kept}"),
    "kept_fresh": ("The folder {folder} and everything built in it so far will be deleted.",
                   "Le dossier {folder} et tout ce qui y a été compilé jusqu'ici seront supprimés.",
                   "Der Ordner {folder} und alles, was bisher darin erstellt wurde, wird gelöscht.",
                   "La carpeta {folder} y todo lo compilado en ella hasta ahora se eliminarán.",
                   "La cartella {folder} e tutto ciò che è stato compilato finora al suo interno verranno eliminati."),
    "kept_old": ("Your earlier install in this folder stays as it was.",
                 "Votre installation précédente dans ce dossier reste telle quelle.",
                 "Deine frühere Installation in diesem Ordner bleibt unverändert.",
                 "Tu instalación anterior en esta carpeta se queda como estaba.",
                 "La tua installazione precedente in questa cartella resta com'era."),
    "keep_installing": ("Keep installing", "Continuer l'installation", "Weiter installieren",
                        "Seguir instalando", "Continua l'installazione"),
    "cancel_install": ("Cancel the installation", "Annuler l'installation", "Installation abbrechen",
                       "Cancelar la instalación", "Annulla l'installazione"),
    "cancelling": ("Cancelling: stopping the build and deleting what it made",
                   "Annulation : arrêt de la compilation et suppression de ce qu'elle a produit",
                   "Abbruch: Build wird gestoppt und Erzeugtes gelöscht",
                   "Cancelando: se detiene la compilación y se borra lo que generó",
                   "Annullamento: arresto della compilazione ed eliminazione di ciò che ha prodotto"),
    # the job
    "getting_files": ("Getting the game's files", "Récupération des fichiers du jeu",
                      "Spieldateien werden geholt", "Obteniendo los archivos del juego",
                      "Recupero dei file del gioco"),
    "not_empty": ("{target} is not empty. Pick an empty folder, or one with an earlier install.",
                  "{target} n'est pas vide. Choisissez un dossier vide, ou contenant une installation précédente.",
                  "{target} ist nicht leer. Wähle einen leeren Ordner oder einen mit einer früheren Installation.",
                  "{target} no está vacía. Elige una carpeta vacía o una con una instalación anterior.",
                  "{target} non è vuota. Scegli una cartella vuota o con un'installazione precedente."),
    "no_release": ("No release yet: cloning the repository ({ref})",
                   "Pas encore de version : clonage du dépôt ({ref})",
                   "Noch keine Version: Repository wird geklont ({ref})",
                   "Aún no hay versión: clonando el repositorio ({ref})",
                   "Nessuna versione ancora: clonazione del repository ({ref})"),
    "dl_fail": ("Could not download the game's files. Is the Deck online?",
                "Impossible de télécharger les fichiers du jeu. Le Steam Deck est-il en ligne ?",
                "Die Spieldateien konnten nicht heruntergeladen werden. Ist das Steam Deck online?",
                "No se pudieron descargar los archivos del juego. ¿Está la Steam Deck conectada?",
                "Impossibile scaricare i file del gioco. Lo Steam Deck è online?"),
    "downloading": ("Downloading {tag}", "Téléchargement de {tag}", "{tag} wird heruntergeladen",
                    "Descargando {tag}", "Download di {tag}"),
    "checksum": ("The download does not match its checksum. Try again.",
                 "Le téléchargement ne correspond pas à sa somme de contrôle. Réessayez.",
                 "Der Download stimmt nicht mit seiner Prüfsumme überein. Versuche es erneut.",
                 "La descarga no coincide con su suma de comprobación. Inténtalo de nuevo.",
                 "Il download non corrisponde al suo checksum. Riprova."),
    "unpacking": ("Unpacking", "Décompression", "Entpacken", "Descomprimiendo", "Estrazione"),
    "not_release": ("That archive is not a game release.", "Cette archive n'est pas une version du jeu.",
                    "Dieses Archiv ist keine Spielversion.", "Ese archivo no es una versión del juego.",
                    "Quell'archivio non è una versione del gioco."),
    "starting_build": ("Starting the build", "Démarrage de la compilation", "Build wird gestartet",
                       "Iniciando la compilación", "Avvio della compilazione"),
    "building": ("Building {n}/{m}", "Compilation {n}/{m}", "Build {n}/{m}", "Compilando {n}/{m}",
                 "Compilazione {n}/{m}"),
    "build_failed": ("The build failed: {why}\nRunning this installer again goes on where it stopped. The whole log is in build/setup.log.",
                     "La compilation a échoué : {why}\nRelancer cet installateur reprend là où il s'est arrêté. Le journal complet est dans build/setup.log.",
                     "Der Build ist fehlgeschlagen: {why}\nWenn du diesen Installer erneut startest, geht es dort weiter, wo er aufgehört hat. Das vollständige Protokoll steht in build/setup.log.",
                     "La compilación falló: {why}\nSi vuelves a ejecutar este instalador, continúa donde se detuvo. El registro completo está en build/setup.log.",
                     "La compilazione non è riuscita: {why}\nRieseguendo questo installatore si riprende da dove si era interrotto. Il registro completo è in build/setup.log."),
    "adding_extra": ("Adding the {label}", "Ajout : {label}", "{label} werden hinzugefügt",
                     "Añadiendo: {label}", "Aggiunta: {label}"),
    "adding_saves": ("Importing your PCSX2 saves", "Import de vos sauvegardes PCSX2",
                     "Deine PCSX2-Spielstände werden importiert", "Importando tus partidas de PCSX2",
                     "Importazione dei tuoi salvataggi PCSX2"),
    "extra_japanese": ("Japanese voices and movies", "voix et films japonais", "Japanische Stimmen und Videos",
                       "voces y películas japonesas", "voci e filmati giapponesi"),
    "extra_european": ("European texts", "textes européens", "Europäische Texte", "textos europeos",
                       "testi europei"),
    "unexpected": ("Unexpected error: {error}", "Erreur inattendue : {error}", "Unerwarteter Fehler: {error}",
                   "Error inesperado: {error}", "Errore imprevisto: {error}"),
    "adding_steam": ("Adding the game to Steam", "Ajout du jeu à Steam", "Spiel wird zu Steam hinzugefügt",
                     "Añadiendo el juego a Steam", "Aggiunta del gioco a Steam"),
    # what the build script is doing (the bar's text)
    "ph_container": ("Creating the build container", "Création du conteneur de compilation",
                     "Build-Container wird erstellt", "Creando el contenedor de compilación",
                     "Creazione del container di compilazione"),
    "ph_tools": ("Installing the build tools", "Installation des outils de compilation",
                 "Build-Werkzeuge werden installiert", "Instalando las herramientas de compilación",
                 "Installazione degli strumenti di compilazione"),
    "ph_in_container": ("Building in the container", "Compilation dans le conteneur", "Build im Container",
                        "Compilando en el contenedor", "Compilazione nel container"),
    "ph_verify": ("Checking the disc image", "Vérification de l'image disque", "Disc-Image wird geprüft",
                  "Comprobando la imagen de disco", "Verifica dell'immagine del disco"),
    "ph_extract": ("Extracting the disc", "Extraction du disque", "Disc wird entpackt", "Extrayendo el disco",
                   "Estrazione del disco"),
    "ph_patches": ("Applying the patches", "Application des correctifs", "Patches werden angewendet",
                   "Aplicando los parches", "Applicazione delle patch"),
    "ph_recompiler": ("Building the recompiler", "Compilation du recompilateur", "Recompiler wird erstellt",
                      "Compilando el recompilador", "Compilazione del ricompilatore"),
    "ph_recompile": ("Recompiling the game", "Recompilation du jeu", "Spiel wird rekompiliert",
                     "Recompilando el juego", "Ricompilazione del gioco"),
    "ph_configure": ("Configuring the game's build", "Configuration de la compilation du jeu",
                     "Build des Spiels wird konfiguriert", "Configurando la compilación del juego",
                     "Configurazione della compilazione del gioco"),
    "ph_game": ("Building the game", "Compilation du jeu", "Spiel wird erstellt", "Compilando el juego",
                "Compilazione del gioco"),
    # the tips shown while it builds (one after the other, every few seconds)
    "tips_title": ("While you wait", "Pendant que vous patientez", "Während du wartest", "Mientras esperas",
                   "Nell'attesa"),
    "tipt_why": ('Why the wait?', 'Pourquoi attendre ?', 'Warum das Warten?', '¿Por qué esperar?', 'Perché attendere?'),
    "tipt_menu": ("The game's menu", 'Le menu du jeu', 'Das Spielmenü', 'El menú del juego', 'Il menu del gioco'),
    "tipt_home": ('A home screen', "Un écran d'accueil", 'Ein Startbildschirm', 'Una pantalla de inicio', 'Una schermata iniziale'),
    "tipt_states": ('Save states', 'Sauvegardes rapides', 'Schnellspeicher', 'Estados guardados', 'Salvataggi rapidi'),
    "tipt_back": ('Back buttons', 'Boutons arrière', 'Rückentasten', 'Botones traseros', 'Tasti posteriori'),
    "tipt_fps": ('60 frames per second', '60 images par seconde', '60 Bilder pro Sekunde', '60 fotogramas por segundo', '60 fotogrammi al secondo'),
    "tipt_options": ('Your own game', 'Un jeu à votre main', 'Dein eigenes Spiel', 'Un juego a tu medida', 'Un gioco su misura'),
    "tipt_achievements": ('130 achievements', '130 succès', '130 Erfolge', '130 logros', '130 obiettivi'),
    "tipt_languages": ('Five languages', 'Cinq langues', 'Fünf Sprachen', 'Cinco idiomas', 'Cinque lingue'),
    "tip_menu": ("L4 opens the game's menu at any time: options, save states, achievements, language.",
                 "L4 ouvre à tout moment le menu du jeu : options, sauvegardes rapides, succès, langue.",
                 "L4 öffnet jederzeit das Spielmenü: Optionen, Schnellspeicher, Erfolge, Sprache.",
                 "L4 abre en cualquier momento el menú del juego: opciones, estados guardados, logros, idioma.",
                 "L4 apre in qualsiasi momento il menu del gioco: opzioni, salvataggi rapidi, obiettivi, lingua."),
    "tip_states": ("Five slots hold the whole game at one moment, and they survive closing the game.",
                   "Cinq emplacements gardent tout le jeu à un instant donné, même après avoir fermé le jeu.",
                   "Fünf Slots halten das ganze Spiel in einem Moment fest, auch nach dem Schließen.",
                   "Cinco ranuras guardan todo el juego en un momento dado, incluso tras cerrarlo.",
                   "Cinque slot conservano l'intero gioco in un istante, anche dopo averlo chiuso."),
    "tip_back": ("Added to Steam, the game uses the Deck's back buttons: L4 opens the menu, L5 pauses, R4 saves a state, R5 loads one.",
                 "Une fois ajouté à Steam, le jeu utilise les boutons arrière : L4 ouvre le menu, L5 met en pause, R4 sauvegarde, R5 charge.",
                 "In Steam eingetragen, nutzt das Spiel die Rückentasten des Decks: L4 öffnet das Menü, L5 pausiert, R4 speichert, R5 lädt.",
                 "Añadido a Steam, el juego usa los botones traseros: L4 abre el menú, L5 pausa, R4 guarda un estado y R5 carga uno.",
                 "Aggiunto a Steam, il gioco usa i tasti posteriori: L4 apre il menu, L5 mette in pausa, R4 salva, R5 carica."),
    "tip_home": ("The game opens on a home screen with Play, Update and Quit. Update fetches new versions without this installer.",
                 "Le jeu s'ouvre sur un écran d'accueil : Jouer, Mettre à jour, Quitter. Mettre à jour récupère les nouvelles versions sans cet installateur.",
                 "Das Spiel startet mit einem Startbildschirm: Spielen, Aktualisieren, Beenden. Aktualisieren holt neue Versionen ohne diesen Installer.",
                 "El juego se abre en una pantalla de inicio: Jugar, Actualizar, Salir. Actualizar obtiene nuevas versiones sin este instalador.",
                 "Il gioco si apre su una schermata iniziale: Gioca, Aggiorna, Esci. Aggiorna scarica le nuove versioni senza questo installatore."),
    "tip_fps": ("The game runs at 60 frames per second instead of the original 30; the menu can put the 30 back.",
                "Le jeu tourne à 60 images par seconde au lieu des 30 d'origine ; le menu permet de remettre les 30.",
                "Das Spiel läuft mit 60 statt der originalen 30 Bildern pro Sekunde; im Menü lassen sich die 30 wiederherstellen.",
                "El juego va a 60 fotogramas por segundo en vez de los 30 originales; el menú permite volver a los 30.",
                "Il gioco gira a 60 fotogrammi al secondo invece dei 30 originali; dal menu si possono ripristinare i 30."),
    "tip_options": ("Make it your own game: encounter rate, experience, money, drops, shop prices, camera distance, field of view...",
                    "Un jeu à votre main : fréquence des combats, expérience, argent, butin, prix des boutiques, distance de caméra, champ de vision...",
                    "Mach das Spiel zu deinem: Begegnungsrate, Erfahrung, Geld, Beute, Ladenpreise, Kameraabstand, Sichtfeld...",
                    "Un juego a tu medida: frecuencia de combates, experiencia, dinero, botín, precios, distancia de cámara, campo de visión...",
                    "Un gioco su misura: frequenza degli scontri, esperienza, denaro, bottino, prezzi, distanza della camera, campo visivo..."),
    "tip_achievements_on": ("Your RetroAchievements account is connected: the 130 achievements are on from the first launch.",
                            "Votre compte RetroAchievements est connecté : les 130 succès sont actifs dès le premier lancement.",
                            "Dein RetroAchievements-Konto ist verbunden: Die 130 Erfolge sind ab dem ersten Start aktiv.",
                            "Tu cuenta de RetroAchievements está conectada: los 130 logros estarán activos desde el primer inicio.",
                            "Il tuo account RetroAchievements è collegato: i 130 obiettivi sono attivi fin dal primo avvio."),
    "tip_achievements": ("The game's 130 RetroAchievements can be turned on later, from the menu's Achievements page.",
                         "Les 130 succès RetroAchievements du jeu peuvent être activés plus tard, depuis la page Succès du menu.",
                         "Die 130 RetroAchievements des Spiels lassen sich später auf der Erfolge-Seite des Menüs aktivieren.",
                         "Los 130 logros de RetroAchievements se pueden activar más tarde, desde la página Logros del menú.",
                         "I 130 obiettivi RetroAchievements si possono attivare più tardi dalla pagina Obiettivi del menu."),
    "tip_languages": ("The menu speaks English, French, German, Spanish and Italian; the game's texts in those languages need the European disc.",
                      "Le menu parle anglais, français, allemand, espagnol et italien ; les textes du jeu dans ces langues demandent le disque européen.",
                      "Das Menü spricht Englisch, Französisch, Deutsch, Spanisch und Italienisch; die Spieltexte in diesen Sprachen brauchen die europäische Disc.",
                      "El menú habla inglés, francés, alemán, español e italiano; los textos del juego en esos idiomas requieren el disco europeo.",
                      "Il menu parla inglese, francese, tedesco, spagnolo e italiano; i testi del gioco in queste lingue richiedono il disco europeo."),
    "tip_why": ("The game is rebuilt here, from your own disc, into a program made for this Deck. Nothing of it leaves the Deck.",
                "Le jeu est recompilé ici, à partir de votre disque, en un programme fait pour ce Steam Deck. Rien n'en sort.",
                "Das Spiel wird hier aus deiner Disc in ein Programm für dieses Deck umgebaut. Nichts verlässt das Gerät.",
                "El juego se reconstruye aquí, desde tu disco, en un programa hecho para esta Steam Deck. Nada sale de ella.",
                "Il gioco viene ricostruito qui, dal tuo disco, in un programma fatto per questo Steam Deck. Nulla esce dal dispositivo."),
}

TIP_SECONDS = 9  # how long each tip stays on the install page
TIPS = ("tip_why", "tip_menu", "tip_home", "tip_states", "tip_back", "tip_fps", "tip_options",
        "tip_achievements", "tip_languages")


def tr(key: str, **values) -> str:
    """The text for this key in the window's language (English if it has none)."""
    texts = STRINGS[key]
    text = texts[CODES.index(_current[0])] if CODES.index(_current[0]) < len(texts) else texts[0]
    return text.format(**values) if values else text


def set_language(code: str) -> None:
    _current[0] = code if code in CODES else "en"


def locale_language(value: str) -> str | None:
    """'fr_FR.UTF-8' -> 'fr', for a language this window has. None for an unknown one."""
    code = value.replace("-", "_").split(".")[0].split("_")[0].strip().lower()
    return code if code in CODES else None


def steam_language() -> str | None:
    """The language in Steam's own settings (the Deck's): None when there is none, "en" for one this window
    has no texts for."""
    try:
        text = (Path.home() / ".steam" / "registry.vdf").read_text(errors="replace")
    except OSError:
        return None
    match = re.search(r'"language"\s+"(\w+)"', text)
    return STEAM_LANGUAGES.get(match.group(1).lower(), "en") if match else None


def detect_language() -> str:
    """The Deck's language: RG_LANG for a run of one's own, else Steam's language setting, else the system's
    locale, else English."""
    forced = locale_language(os.environ.get("RG_LANG", ""))
    if forced:
        return forced
    steam = steam_language()
    if steam:
        return steam
    for name in ("LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"):
        value = next((p for p in os.environ.get(name, "").split(":") if p and p not in ("C", "POSIX")), "")
        if value:
            return locale_language(value) or "en"
    return "en"


# ---------------------------------------------------------------- helpers


def free_gb(path: Path) -> float:
    probe = path
    while not probe.exists() and probe != probe.parent:
        probe = probe.parent
    return shutil.disk_usage(probe).free / 2**30


def storage_label(code: str) -> str:
    """The name of a place: "internal" and "sd" are translated, a drive keeps its own name."""
    return {"internal": lambda: tr("storage_internal"), "sd": lambda: tr("storage_sd")}.get(code, lambda: code)()


def storage_choices() -> list[tuple[str, Path]]:
    """Where the game can go: the Deck's own storage, and each card or drive (a code for storage_label)."""
    choices = [("internal", Path.home() / "Games" / "RogueGalaxy")]
    seen = set()
    for base in (Path("/run/media"), Path("/run/media/deck")):
        if not base.is_dir():
            continue
        for entry in sorted(base.iterdir()):
            if entry.is_dir() and os.path.ismount(entry) and entry.resolve() not in seen:
                seen.add(entry.resolve())
                label = "sd" if entry.name.upper().startswith("SD") or entry.name == "mmcblk0p1" else entry.name
                choices.append((label, entry / "Games" / "RogueGalaxy"))
    return choices


# The Steam library's pictures for the game, from installer/art/steam: file -> name in the account's
# config/grid folder ({id} is the shortcut's number).
STEAM_ART = (("capsule.png", "{id}", ".png"), ("portrait.png", "{id}p", ".png"),
             ("hero.png", "{id}_hero", ".png"), ("logo.png", "{id}_logo", ".png"),
             ("icon.ico", "{id}_icon", ".ico"), ("logo-position.json", "{id}", ".json"))
PICTURE_SUFFIXES = (".png", ".jpg", ".jpeg", ".webp", ".ico")


def steam_userdata() -> Path:
    return Path(os.environ.get("RG_STEAM_USERDATA") or Path.home() / ".local/share/Steam/userdata")


def steam_shortcuts(script: Path) -> list[tuple[Path, int, str]]:
    """(an account's config folder, the shortcut's number, its name) for each Steam shortcut that starts this script."""
    found = []
    for vdf in steam_userdata().glob("*/config/shortcuts.vdf"):
        try:
            data = vdf.read_bytes()
        except OSError:
            continue
        # Each entry starts with its number (appid); its Exe comes before the next entry's.
        for part in re.split(rb"\x02appid\x00", data)[1:]:
            match = re.search(rb"\x01[Ee]xe\x00(.*?)\x00", part)
            named = re.search(rb"\x01[Aa]pp[Nn]ame\x00(.*?)\x00", part)
            if len(part) < 4 or not match:
                continue
            exe = Path(match.group(1).decode(errors="replace").strip('"'))
            try:
                if exe.exists() and script.exists() and os.path.samefile(exe, script):
                    found.append((vdf.parent, struct.unpack("<I", part[:4])[0],
                                  named.group(1).decode(errors="replace") if named else "Rogue Galaxy"))
            except OSError:
                continue
    return found


def put_steam_art(art: Path, config: Path, appid: int) -> int:
    """Copies the pictures into config/grid for this shortcut, leaving any picture already there.
    Returns how many it copied."""
    grid = config / "grid"
    grid.mkdir(parents=True, exist_ok=True)
    copied = 0
    for source, stem, suffix in STEAM_ART:
        origin, name = art / source, stem.format(id=appid)
        if not origin.is_file():
            continue
        if suffix == ".json":
            taken = (grid / f"{name}.json").exists()
        else:
            taken = any(p.suffix.lower() in PICTURE_SUFFIXES for p in grid.glob(name + ".*"))
        if not taken:
            shutil.copyfile(origin, grid / f"{name}{suffix}")
            copied += 1
    return copied


# Steam Input: the Deck's back buttons, as keys the game reads (L4 the menu, L5 the pause, R4 quick save,
# R5 quick load; docs/SAVESTATES.md). Steam names them button_back_left_upper (L4), button_back_left (L5),
# button_back_right_upper (R4) and button_back_right (R5).
STEAM_INPUT_KEYS = (("button_back_left_upper", "F1"), ("button_back_left", "F5"),
                    ("button_back_right_upper", "F2"), ("button_back_right", "F3"))
STEAM_INPUT_TEMPLATE = "controller_neptune_gamepad_joystick.vdf"  # "Gamepad With Joystick Trackpad"


def steam_input_config(template: str) -> str | None:
    """Steam's own template text with the four keys on the back buttons, or None when it has no such button."""
    text = template.lstrip("\ufeff")
    for button, key in STEAM_INPUT_KEYS:
        text, found = re.subn(
            r'("%s"\s*\{\s*"activators"\s*\{\s*"Full_Press"\s*\{\s*"bindings"\s*\{)\s*\}' % button,
            lambda m, key=key: m.group(1) + '\n\t\t\t\t\t\t\t"binding"\t\t"key_press %s, , "\n\t\t\t\t\t\t}' % key,
            text, count=1)
        if not found:
            return None
    return text.replace('"description" "#Description"', '"description"\t\t"#SettingsController_AutosaveDescription"', 1)


def put_steam_input(config: Path, template_dir: Path, name: str) -> bool:
    """Gives the game's Steam entry a Steam Deck controller layout with the keys above, in the account's
    Steam Controller Configs folder, as Steam writes a personal layout. A layout already there is the
    player's: left alone. Returns whether it wrote one."""
    name = name.lower()  # Steam names the folder after the shortcut, in lower case
    root = config.parent.parent.parent  # the Steam folder
    account = config.parent.name
    base = root / "steamapps" / "common" / "Steam Controller Configs" / account / "config"
    target = base / name / "controller_neptune.vdf"
    if target.exists():
        return False
    try:
        template = (template_dir / STEAM_INPUT_TEMPLATE).read_text(encoding="utf-8-sig")
    except OSError:
        return False
    text = steam_input_config(template)
    if text is None:
        return False
    text = text.replace('"title" "#Title"', '"title"\t\t"#Title"', 1)
    text = text.replace('"controller_type"\t\t"controller_neptune"',
                        '"progenitor"\t\t"template://%s"\n\t"url"\t\t"autosave://%s"\n'
                        '\t"export_type"\t\t"unknown"\n\t"controller_type"\t\t"controller_neptune"'
                        % (STEAM_INPUT_TEMPLATE, target), 1)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    # Steam keeps which layout a game uses in configset_controller_neptune.vdf.
    configset = base / "configset_controller_neptune.vdf"
    try:
        current = configset.read_text(encoding="utf-8")
    except OSError:
        current = '"controller_config"\n{\n}\n'
    if f'"{name}"' not in current:
        entry = f'\t"{name}"\n\t{{\n\t\t"autosave"\t\t"1"\n\t}}\n'
        end = current.rstrip().rfind("}")
        if end >= 0:
            configset.write_text(current[:end] + entry + current[end:], encoding="utf-8")
    return True


def looks_like_iso(path: Path) -> str | None:
    """None when it may be the disc image, else the key (of tr) of what is wrong with it."""
    if not path.is_file():
        return "iso_missing"
    if path.suffix.lower() != ".iso":
        return "iso_ext"
    size = path.stat().st_size
    if size < 4 * 2**30:
        return "iso_small"
    with open(path, "rb") as f:
        f.seek(0x8001)
        if f.read(5) != b"CD001":
            return "iso_notdisc"
    return None


RA_LOGIN = Path.home() / ".local/share/RogueGalaxy/ra/login.json"


def ra_sign_in(user: str, password: str):
    """Signs in to RetroAchievements and keeps the connection token (never the password) where the game reads
    it, mode 600. Returns (done, a function giving what to tell the player, in the window's language)."""
    body = urllib.parse.urlencode({"r": "login2", "u": user, "p": password}).encode()
    request = urllib.request.Request("https://retroachievements.org/dorequest.php", data=body, method="POST",
                                     headers={"User-Agent": "RogueGalaxyRecompiledSetup/1"})
    try:
        try:
            with urllib.request.urlopen(request, timeout=30) as answer:
                reply = json.load(answer)
        except urllib.error.HTTPError as error:  # a refusal comes with a status and the reason as JSON
            reply = json.load(error)
    except (OSError, ValueError):
        return False, lambda: tr("ra_unreachable")
    if not reply.get("Success") or not reply.get("Token"):
        reason = reply.get("Error")  # the service's own words
        return False, (lambda: str(reason)) if reason else (lambda: tr("ra_refused"))
    RA_LOGIN.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(RA_LOGIN, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump({"user": reply.get("User", user), "token": reply["Token"]}, f)
    os.chmod(RA_LOGIN, 0o600)
    name = reply.get("User", user)
    return True, lambda: tr("ra_signed_as", user=name)


# ---------------------------------------------------------------- PCSX2 saves

SAVE_PREFIX = "BASCUS-97490"  # the USA release's save folders: BASCUS-97490LV5RG_00, _01...


def card_roots() -> list[Path]:
    """Where PCSX2 (Flatpak, AppImage, EmuDeck, RetroDECK) keeps its memory cards on a Deck."""
    home = Path.home()
    roots = [home / ".var/app/net.pcsx2.PCSX2/config/PCSX2/memcards", home / ".config/PCSX2/memcards",
             home / "Documents/PCSX2/memcards", home / ".local/share/PCSX2/memcards",
             home / "Emulation/saves/pcsx2/saves", home / "Emulation/saves/pcsx2/memcards",
             home / "retrodeck/saves/PS2/pcsx2/memcards", home / "retrodeck/saves/ps2/memcards"]
    for card in Path("/run/media").glob("*/*"):  # the SD card
        roots += [card / "Emulation/saves/pcsx2/saves", card / "Emulation/saves/pcsx2/memcards",
                  card / "retrodeck/saves/PS2/pcsx2/memcards"]
    return roots


def ps2_card_saves(path: Path) -> dict[str, dict[str, bytes]]:
    """The Rogue Galaxy saves of a raw PCSX2 memory card (a .ps2 file): folder name -> {file name: bytes}.
    Reads the card's own file system: superblock, FAT, directory entries."""
    data = path.read_bytes()
    if data[:28] != b"Sony PS2 Memory Card Format ":
        raise ValueError("not a PS2 memory card")
    page, per_cluster, _blk, _ = struct.unpack_from("<4H", data, 0x28)
    clusters, alloc_offset, _alloc_end, root = struct.unpack_from("<4I", data, 0x30)
    stride = page + 16 if len(data) >= clusters * per_cluster * (page + 16) else page  # with or without ECC
    size = page * per_cluster

    def cluster(n: int) -> bytes:
        return b"".join(data[(n * per_cluster + i) * stride:(n * per_cluster + i) * stride + page]
                        for i in range(per_cluster))

    per = size // 4
    fat_clusters: list[int] = []
    for indirect in struct.unpack_from("<32I", data, 0x50):
        if indirect != 0xFFFFFFFF and indirect < clusters:
            fat_clusters += [c for c in struct.unpack(f"<{per}I", cluster(indirect)) if c < clusters]
    fat: list[int] = []
    for fc in fat_clusters:
        fat += struct.unpack(f"<{per}I", cluster(fc))

    def chain(first: int) -> bytes:
        out, seen, c = [], set(), first
        while c not in seen and c < len(fat):
            seen.add(c)
            out.append(cluster(alloc_offset + c))
            nxt = fat[c]
            if nxt == 0xFFFFFFFF or not nxt & 0x80000000:
                break
            c = nxt & 0x7FFFFFFF
        return b"".join(out)

    def entries(first: int) -> list[tuple[int, int, int, str]]:
        raw = chain(first)
        count = struct.unpack_from("<I", raw, 4)[0]
        found = []
        for i in range(min(count, len(raw) // 512)):
            mode, length, start = struct.unpack_from("<HxxI8xI", raw, i * 512)
            name = raw[i * 512 + 0x40:i * 512 + 0x60].split(b"\0")[0].decode("ascii", "replace")
            if mode & 0x8000:
                found.append((mode, length, start, name))
        return found

    saves: dict[str, dict[str, bytes]] = {}
    for mode, _length, start, name in entries(root):
        if not (mode & 0x20 and name.startswith(SAVE_PREFIX)):  # 0x20 a directory, 0x10 a file
            continue
        files = {}
        for fmode, flen, fstart, fname in entries(start):
            if fmode & 0x10 and fname not in (".", ".."):
                files[fname] = chain(fstart)[:flen]
        saves[name] = files
    return saves


def folder_card_saves(path: Path) -> list[Path]:
    """The Rogue Galaxy save folders of a PCSX2 folder memory card (or of a plain folder holding them)."""
    try:
        return sorted(d for d in path.iterdir() if d.is_dir() and d.name.startswith(SAVE_PREFIX))
    except OSError:
        return []


def list_saves(path: Path) -> list[str]:
    """The names of the saves in a card (file or folder); an empty list when there is none (or it is no card)."""
    try:
        if path.is_dir():
            return [d.name for d in folder_card_saves(path)]
        return sorted(ps2_card_saves(path))
    except (OSError, ValueError, struct.error):
        return []


def find_cards() -> list[Path]:
    """The cards PCSX2 left on this Deck that hold a Rogue Galaxy save, the most recently saved first."""
    found: list[tuple[float, Path]] = []
    for root in card_roots():
        try:
            children = sorted(root.iterdir())
        except OSError:
            continue
        for card in children:
            if card.is_file() and card.suffix.lower() != ".ps2":
                continue
            if not list_saves(card):
                continue
            try:
                if card.is_dir():
                    when = max(d.stat().st_mtime for d in folder_card_saves(card))
                else:
                    when = card.stat().st_mtime
            except OSError:
                when = 0.0
            found.append((when, card))
    found.sort(key=lambda item: item[0], reverse=True)
    return [card for _, card in found]


def import_saves(source: Path, mc0: Path) -> tuple[list[str], list[str]]:
    """Copy the card's Rogue Galaxy saves into the game's memory card folder. A save already there is never
    replaced. Returns (copied, kept) names."""
    copied, kept = [], []
    mc0.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        wanted = {d.name: d for d in folder_card_saves(source)}
        for name, folder in wanted.items():
            if (mc0 / name).exists():
                kept.append(name)
            else:
                shutil.copytree(folder, mc0 / name)
                copied.append(name)
    else:
        for name, files in ps2_card_saves(source).items():
            if (mc0 / name).exists():
                kept.append(name)
                continue
            (mc0 / name).mkdir()
            for fname, blob in files.items():
                (mc0 / name / fname).write_bytes(blob)
            copied.append(name)
    return copied, kept


class KeepAwake:
    """No sleep and no dimming while it is held (an install takes 10 to 30 minutes). Three inhibitors:
    systemd's (a systemd-inhibit that lasts as long as its stdin stays open, so it also ends if this
    program dies), and, in desktop mode, KDE's power and screen-saver ones."""

    KDE = (("org.freedesktop.PowerManagement.Inhibit", "/org/freedesktop/PowerManagement/Inhibit",
            "org.freedesktop.PowerManagement.Inhibit"),
           ("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver"))

    def __init__(self) -> None:
        self.process: subprocess.Popen | None = None
        self.bus = None
        self.cookies: list[tuple[str, str, str, int]] = []

    def start(self, reason: str) -> None:
        if shutil.which("systemd-inhibit"):
            try:
                self.process = subprocess.Popen(
                    ["systemd-inhibit", "--what=sleep:idle", "--who=Rogue Galaxy Setup", f"--why={reason}",
                     "--mode=block", "cat"], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL)
            except OSError:
                self.process = None
        try:
            self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        except GLib.Error:
            return
        for name, path, iface in self.KDE:
            try:
                answer = self.bus.call_sync(name, path, iface, "Inhibit",
                                            GLib.Variant("(ss)", ("Rogue Galaxy Setup", reason)),
                                            GLib.VariantType("(u)"), Gio.DBusCallFlags.NONE, 2000, None)
                self.cookies.append((name, path, iface, answer.unpack()[0]))
            except GLib.Error:
                continue  # not running (game mode has no KDE)

    def stop(self) -> None:
        for name, path, iface, cookie in self.cookies:
            try:
                self.bus.call_sync(name, path, iface, "UnInhibit", GLib.Variant("(u)", (cookie,)), None,
                                   Gio.DBusCallFlags.NONE, 2000, None)
            except GLib.Error:
                pass
        self.cookies = []
        if self.process:
            try:
                self.process.stdin.close()
                self.process.wait(timeout=3)
            except (OSError, subprocess.SubprocessError):
                self.process.kill()
            self.process = None


class Failure(Exception):
    pass


class Cancelled(Exception):
    pass


# ---------------------------------------------------------------- the job


class Job(threading.Thread):
    """Everything after the pages: fetch, build, extras, Steam."""

    # What each of install.sh's / setup.py's "== ..." lines is worth on the
    # bar, and where its ninja counter ("[12/340]") fills it.
    PHASES = [
        ("== Creating the build container", 0.04, 0.10, "ph_container"),
        ("== Installing the build tools", 0.10, 0.16, "ph_tools"),
        ("== Building in the container", 0.16, 0.16, "ph_in_container"),
        ("== verify", 0.16, 0.17, "ph_verify"),
        ("== extract", 0.17, 0.24, "ph_extract"),
        ("== patches", 0.24, 0.25, "ph_patches"),
        ("== build ps2_recomp", 0.25, 0.34, "ph_recompiler"),
        ("== recompile", 0.34, 0.38, "ph_recompile"),
        ("== configure build/game", 0.38, 0.40, "ph_configure"),
        ("== build the game", 0.40, 0.94, "ph_game"),
    ]

    def __init__(self, cfg: argparse.Namespace, ui: "Window"):
        super().__init__(daemon=True)
        self.cfg = cfg
        self.ui = ui
        self.lo, self.hi = 0.0, 0.0
        self.tail: list[str] = []
        self.cancelled = False
        self.killed = threading.Event()  # the build's processes are gone
        self.proc: subprocess.Popen | None = None
        folder = Path(cfg.dir) if cfg.dir else None
        # A folder this install creates (new, or empty) goes away if it is cancelled; one with an
        # earlier install in it stays as it was.
        self.fresh = bool(folder and not (folder / "install.sh").exists()
                          and (not folder.exists() or not any(folder.iterdir())))

    def say(self, text: str, fraction: float | None = None) -> None:
        GLib.idle_add(self.ui.on_progress, text, fraction)

    def log(self, line: str) -> None:
        GLib.idle_add(self.ui.on_log, line)
        self.tail = (self.tail + [line])[-40:] if line.strip() else self.tail
        try:  # kept for a report: <folder>/build/setup.log
            (self.cfg.dir / "build").mkdir(parents=True, exist_ok=True)
            with open(self.cfg.dir / "build" / "setup.log", "a", encoding="utf-8") as f:
                f.write(line + "\n")
        except OSError:
            pass

    def why(self) -> str:
        """The last lines of output that look like an explanation."""
        lines = [l for l in self.tail if l.strip() and not l.startswith(("[", "== ", "$ "))]
        return " ".join(lines[-2:])[:300]

    def run(self) -> None:
        if self.cfg.demo:
            return self.run_demo()
        try:
            self.fetch()
            self.build()
            self.extras()
            self.saves()
            self.steam()
        except Cancelled:
            self.killed.wait(90)
            self.cleanup()
            GLib.idle_add(self.ui.on_cancelled)
            return
        except Failure as error:
            GLib.idle_add(self.ui.on_failed, str(error))
            return
        except Exception as error:  # a bug here must still reach the window
            GLib.idle_add(self.ui.on_failed, tr("unexpected", error=error))
            return
        GLib.idle_add(self.ui.on_done)

    def run_demo(self) -> None:
        """--demo: the install page with a made-up build, to look at it. Touches nothing."""
        steps = [self.PHASES[i] for i in (0, 1, 3, 4, 5, 6, 7, 8, 9)]
        for index, (_line, lo, hi, key) in enumerate(steps):
            last = index == len(steps) - 1
            for n in range(60 if last else 12):
                if self.cancelled:
                    GLib.idle_add(self.ui.on_cancelled)
                    return
                fraction = lo + (hi - lo) * n / (60 if last else 12)
                self.say(tr(key) if not last else tr("building", n=n * 5, m=300), fraction)
                GLib.idle_add(self.ui.on_log, f"demo: {key} {n}")
                time.sleep(0.5)
        GLib.idle_add(self.ui.on_done)

    # -- running a program, its output line by line
    def stream(self, command: list[str], cwd: Path, on_line=None) -> int:
        if self.cancelled:
            raise Cancelled
        self.log("$ " + " ".join(command))
        # Its own process group, so that cancelling can end it and what it started.
        process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, text=True, bufsize=1, errors="replace",
                                   start_new_session=True)
        self.proc = process
        assert process.stdout is not None
        for raw in process.stdout:
            line = raw.rstrip()
            self.log(line)
            if on_line:
                on_line(line)
        status = process.wait()
        if self.cancelled:
            raise Cancelled
        return status

    # -- cancelling: end the build, in the container too, then delete what it made
    def cancel(self) -> None:
        """From another thread: stop everything. run() then cleans up."""
        self.cancelled = True
        try:
            self.kill_build()
        finally:
            self.killed.set()

    def kill_build(self) -> None:
        folder = str(Path(self.cfg.dir).resolve()) if self.cfg.dir else ""
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except OSError:
                pass
        if not folder:
            return
        # The build runs in the distrobox container, which outlives this window: end every process whose
        # working directory is inside the install folder. The container sees this computer's folders under
        # /run/host, and shares its process list (if it did not, the same is done from inside it).
        folders = (folder, "/run/host" + folder)
        for _ in range(3):
            for entry in Path("/proc").iterdir():
                if not entry.name.isdigit() or int(entry.name) == os.getpid():
                    continue
                try:
                    cwd = os.readlink(entry / "cwd")
                except OSError:
                    continue
                if any(cwd == f or cwd.startswith(f + "/") for f in folders):
                    try:
                        os.kill(int(entry.name), signal.SIGKILL)
                    except OSError:
                        pass
            time.sleep(0.5)
        script = ('for p in /proc/[0-9]*; do c=$(readlink "$p/cwd" 2>/dev/null); for d in "$@"; do case "$c" in '
                  '"$d"|"$d"/*) [ "${p#/proc/}" != "$$" ] && kill -KILL "${p#/proc/}" 2>/dev/null;; esac; done; done')
        if shutil.which("distrobox"):
            try:
                subprocess.run(["distrobox", "enter", "rg-build", "--", "sh", "-c", script, "sh", *folders],
                               cwd=Path.home(), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=45)
            except (OSError, subprocess.SubprocessError):
                pass

    def cleanup(self) -> None:
        """What a cancelled install leaves: the folder it created, the partial download, the menu entry."""
        folder = Path(self.cfg.dir) if self.cfg.dir else None
        if not folder:
            return
        (folder.parent / f".{folder.name}-download.zip").unlink(missing_ok=True)
        if not self.fresh:
            return
        entry = Path.home() / ".local/share/applications/rogue-galaxy.desktop"
        try:
            if entry.exists() and str(folder) in entry.read_text(errors="replace"):
                entry.unlink()
        except OSError:
            pass
        shutil.rmtree(folder, ignore_errors=True)

    # -- 1. the sources
    def fetch(self) -> None:
        target: Path = self.cfg.dir
        self.say(tr("getting_files"), 0.0)
        if target.exists() and any(target.iterdir()) and not (target / "install.sh").exists():
            raise Failure(tr("not_empty", target=target))
        target.mkdir(parents=True, exist_ok=True)
        if (target / "install.sh").exists():
            self.log("An earlier install is here: going on with it.")
            return
        archive = self.cfg.zip
        if not archive:
            archive = self.download_release(target)
        if archive:
            self.unpack(Path(archive), target)
            return
        self.say(tr("no_release", ref=REF), None)
        status = self.stream(["git", "clone", "--recurse-submodules", "--branch", REF,
                              f"https://github.com/{REPO}.git", str(target)], target.parent)
        if status != 0 or not (target / "install.sh").exists():
            raise Failure(tr("dl_fail"))

    def download_release(self, target: Path) -> Path | None:
        try:
            request = urllib.request.Request(API, headers={"Accept": "application/vnd.github+json",
                                                           "User-Agent": "RogueGalaxySetup"})
            with urllib.request.urlopen(request, timeout=20) as response:
                releases = json.load(response)
        except Exception as error:
            self.log(f"Release list unavailable: {error}")
            return None
        release = releases[0] if releases else {}
        assets = {a["name"]: a["browser_download_url"] for a in release.get("assets", [])}
        zips = sorted(n for n in assets if n.startswith("RogueGalaxy-") and n.endswith(".zip"))
        if not zips or zips[-1] + ".sha256" not in assets:
            return None
        name = zips[-1]
        tag = release.get("tag_name", "")
        destination = target.parent / f".{target.name}-download.zip"
        self.say(tr("downloading", tag=tag), 0.0)
        digest = hashlib.sha256()
        with urllib.request.urlopen(assets[name], timeout=30) as response, open(destination, "wb") as out:
            total = int(response.headers.get("Content-Length") or 0)
            done = 0
            while chunk := response.read(1 << 20):
                if self.cancelled:
                    raise Cancelled
                out.write(chunk)
                digest.update(chunk)
                done += len(chunk)
                if total:
                    self.say(tr("downloading", tag=tag), 0.04 * done / total)
        with urllib.request.urlopen(assets[name] + ".sha256", timeout=30) as response:
            want = response.read().decode().split()[0]
        if want != digest.hexdigest():
            destination.unlink(missing_ok=True)
            raise Failure(tr("checksum"))
        return destination

    def unpack(self, archive: Path, target: Path) -> None:
        self.say(tr("unpacking"), 0.04)
        with zipfile.ZipFile(archive) as z:
            members = [i for i in z.infolist() if not i.is_dir()]
            for info in members:
                if self.cancelled:
                    raise Cancelled
                name = info.filename.split("/", 1)[1] if "/" in info.filename else ""
                if not name:
                    continue
                path = target / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(z.read(info))
                path.chmod(((info.external_attr >> 16) & 0o777) or 0o644)
        if not (target / "install.sh").exists():
            raise Failure(tr("not_release"))
        # What update.sh keeps to update only what changed, later.
        (target / "build").mkdir(exist_ok=True)
        try:
            revision = (target / "REVISION").read_text().strip()
            changed = int((target / "FILES").stat().st_ctime)
            (target / "build" / "unpacked-archive").write_text(f"{revision} {changed}\n")
            (target / "build" / "submodules-installed").write_text((target / "SUBMODULES").read_text())
        except OSError:
            pass
        if str(archive).endswith("-download.zip"):
            archive.unlink(missing_ok=True)

    # -- 2. the build
    def build(self) -> None:
        target: Path = self.cfg.dir
        self.say(tr("starting_build"), 0.04)
        command = ["sh", "install.sh", "--container"]
        if self.cfg.iso:
            command += ["--iso", str(self.cfg.iso)]
        self.lo, self.hi = 0.04, 0.16

        def on_line(line: str) -> None:
            if line.startswith("["):
                counts = line[1:].split("]", 1)[0]
                if "/" in counts:
                    try:
                        n, m = (int(x) for x in counts.split("/", 1))
                    except ValueError:
                        return
                    if m:
                        self.say(tr("building", n=n, m=m), self.lo + (self.hi - self.lo) * n / m)
                return
            for prefix, lo, hi, key in self.PHASES:
                if line.startswith(prefix):
                    self.lo, self.hi = lo, hi
                    self.say(tr(key), lo if lo == hi else None)
                    return

        status = self.stream(command, target, on_line)
        if status != 0 or not (target / "build/game/src/roguegalaxy").exists():
            raise Failure(tr("build_failed", why=self.why()))

    # -- 3. the optional extras (another disc each)
    def extras(self) -> None:
        for flag, iso, command, label in (
            ("japanese", self.cfg.japanese, "extract-japanese", tr("extra_japanese")),
            ("european", self.cfg.european, "extract-european", tr("extra_european")),
        ):
            if not iso:
                continue
            self.say(tr("adding_extra", label=label), 0.95)
            status = self.stream(["distrobox", "enter", "rg-build", "--", "python3", "setup.py", command,
                                  "--iso", str(iso)], self.cfg.dir)
            if status != 0:
                self.log(f"The {label} could not be added; the game works without them.")

    # -- 4. Steam
    # -- 3b. the player's PCSX2 saves
    def saves(self) -> None:
        if not self.cfg.saves:
            return
        self.say(tr("adding_saves"), 0.97)
        try:
            copied, kept = import_saves(Path(self.cfg.saves), Path(self.cfg.dir) / "disc" / "mc0")
        except Exception as error:  # the game works without them
            self.log(f"The saves could not be imported: {error}")
            return
        self.log(f"Imported {len(copied)} save(s): {', '.join(copied) or 'none'}."
                 + (f" Kept the ones already there: {', '.join(kept)}." if kept else ""))

    def in_steam(self, script: Path) -> bool:
        """Whether some Steam account already has this script as a non-Steam game."""
        for vdf in (Path.home() / ".local/share/Steam/userdata").glob("*/config/shortcuts.vdf"):
            try:
                data = vdf.read_bytes()
            except OSError:
                continue
            # The same file may be named by another path (/run/media/SD, /run/media/deck/SD).
            for match in re.finditer(rb"\x01[Ee]xe\x00(.*?)\x00", data):
                exe = Path(match.group(1).decode(errors="replace").strip('"'))
                try:
                    if exe.exists() and script.exists() and os.path.samefile(exe, script):
                        return True
                except OSError:
                    continue
        return False

    def steam(self) -> None:
        if not self.cfg.steam:
            return
        script = self.cfg.dir / "rogue-galaxy.sh"
        if self.in_steam(script):
            self.log("The game is already in Steam: not added again.")
            self.steam_art(script, wait=False)
            self.steam_input(script, wait=False)
            return
        self.say(tr("adding_steam"), 0.98)
        # install.sh wrote the menu entry (named "Rogue Galaxy", with its icon);
        # Steam takes its name, program and folder from it, with no question
        # asked, as long as it is running (desktop mode).
        desktop = Path.home() / ".local/share/applications/rogue-galaxy.desktop"
        tool = shutil.which("steamos-add-to-steam")
        if not tool or not desktop.exists():
            self.log("Could not add the game to Steam by itself: add rogue-galaxy.sh as a non-Steam game.")
            return
        self.stream([tool, str(desktop)], self.cfg.dir)
        self.steam_art(script, wait=True)
        self.steam_input(script, wait=True)

    def steam_art(self, script: Path, wait: bool) -> None:
        """The game's pictures in the Steam library (capsule, portrait, hero, logo, icon), like the ones a
        game from the store has. Steam may need to be restarted (game mode does it) to show them."""
        art = next((p for p in (Path(__file__).resolve().parent / "art" / "steam",
                                self.cfg.dir / "installer" / "art" / "steam") if p.is_dir()), None)
        if not art:
            return
        shortcuts = steam_shortcuts(script)
        for _ in range(15 if wait else 0):  # Steam writes its shortcut a moment after being asked
            if shortcuts:
                break
            time.sleep(2)
            shortcuts = steam_shortcuts(script)
        if not shortcuts:
            self.log("The game's pictures could not be added to Steam: its shortcut was not found.")
            return
        copied = sum(put_steam_art(art, config, appid) for config, appid, _ in shortcuts)
        self.log(f"Added {copied} pictures to the game's Steam entry." if copied
                 else "The game's Steam entry already has its pictures.")

    def steam_input(self, script: Path, wait: bool) -> None:
        """The back buttons' keys (L4 menu, L5 pause, R4 quick save, R5 quick load) in the game's Steam entry.
        Steam rewrites its controller settings when it quits: restart it (game mode does) to see them."""
        shortcuts = steam_shortcuts(script)
        for _ in range(15 if wait else 0):
            if shortcuts:
                break
            time.sleep(2)
            shortcuts = steam_shortcuts(script)
        if not shortcuts:
            self.log("The controller layout could not be set: the game's Steam shortcut was not found.")
            return
        templates = steam_userdata().parent / "controller_base" / "templates"
        done = sum(put_steam_input(config, templates, name) for config, _, name in shortcuts)
        self.log("Set the Steam Deck back buttons: L4 menu, L5 pause, R4 quick save, R5 quick load." if done
                 else "The game's Steam entry keeps its controller layout.")


# ---------------------------------------------------------------- the sky
# The window's background, drawn here (no game art: it is all gradients):
# a deep-space sky with nebulae, twinkling stars, a ringed planet with its
# moon, and now and then a shooting star.


def rgba(r: float, g: float, b: float, a: float = 1.0) -> Gdk.RGBA:
    return Gdk.RGBA(red=r / 255, green=g / 255, blue=b / 255, alpha=a)


def stops(*pairs) -> list:
    out = []
    for offset, color in pairs:
        stop = Gsk.ColorStop()
        stop.offset = offset
        stop.color = color
        out.append(stop)
    return out


def rect(x: float, y: float, w: float, h: float) -> Graphene.Rect:
    return Graphene.Rect().init(x, y, w, h)


def point(x: float, y: float) -> Graphene.Point:
    return Graphene.Point().init(x, y)


def circle(cx: float, cy: float, r: float) -> Gsk.RoundedRect:
    box = Gsk.RoundedRect()
    box.init_from_rect(rect(cx - r, cy - r, 2 * r, 2 * r), r)
    return box


def art_file() -> Path | None:
    """The background picture: one of your own in ~/.local/share/RogueGalaxy/theme, else installer/art
    next to this file (the AppImage carries it)."""
    for folder in (Path.home() / ".local/share/RogueGalaxy/theme", Path(__file__).resolve().parent / "art"):
        if folder.is_dir():
            for path in sorted(folder.iterdir()):
                if path.suffix.lower() in (".jpg", ".jpeg", ".png"):
                    return path
    return None


class Sky(Gtk.Widget):
    __gtype_name__ = "RogueGalaxySky"

    def __init__(self) -> None:
        super().__init__(hexpand=True, vexpand=True)
        self.art: Gdk.Texture | None = None
        path = art_file()
        if path:
            try:
                self.art = Gdk.Texture.new_from_filename(str(path))
            except GLib.Error:
                pass
        rnd = random.Random(7)
        colors = ((255, 255, 255), (255, 255, 255), (170, 205, 255), (255, 226, 180))
        self.stars = [(rnd.random(), rnd.random(), rnd.choice((1, 1, 1, 2, 2, 3)), rnd.random() * 6.28,
                       0.6 + rnd.random() * 1.8, rnd.choice(colors)) for _ in range(260)]
        self.t0 = time.monotonic()
        if not self.art:  # the drawn sky twinkles; a picture stays still
            GLib.timeout_add(66, self.tick)

    def tick(self) -> bool:
        self.queue_draw()
        return True

    def ring(self, snap: Gtk.Snapshot, cx: float, cy: float, radius: float, front: bool) -> None:
        snap.save()
        snap.translate(point(cx, cy))
        snap.rotate(-18)
        if front:  # the half of the ring that passes in front of the planet
            snap.push_clip(rect(-radius * 4, 0, radius * 8, radius * 4))
        for scale, width, color in ((2.25, 2.0, rgba(255, 228, 160, 0.9)), (2.0, 11.0, rgba(255, 190, 110, 0.25)),
                                    (1.7, 3.0, rgba(255, 214, 150, 0.65))):
            rx, ry = radius * scale, radius * scale * 0.2
            corner = Graphene.Size().init(rx, ry)
            box = Gsk.RoundedRect()
            box.init(rect(-rx, -ry, 2 * rx, 2 * ry), corner, corner, corner, corner)
            snap.append_border(box, [width] * 4, [color] * 4)
        if front:
            snap.pop()
        snap.restore()

    def draw_art(self, snap: Gtk.Snapshot, w: float, h: float) -> None:
        """The picture fills the window, cropped mostly on the right, under a dark veil on the left, where
        the window's cards are."""
        tex = self.art
        scale = max(w / tex.get_width(), h / tex.get_height())
        dw, dh = tex.get_width() * scale, tex.get_height() * scale
        snap.append_scaled_texture(tex, Gsk.ScalingFilter.TRILINEAR, rect((w - dw) * 0.25, (h - dh) / 2, dw, dh))
        full = rect(0, 0, w, h)
        snap.append_linear_gradient(full, point(0, 0), point(w, 0),
                                    stops((0, rgba(3, 5, 22, 0.92)), (0.42, rgba(3, 5, 22, 0.60)),
                                          (0.68, rgba(3, 5, 22, 0.12)), (1, rgba(3, 5, 22, 0))))
        snap.append_linear_gradient(full, point(0, 0), point(0, h),
                                    stops((0, rgba(3, 5, 22, 0.35)), (0.25, rgba(3, 5, 22, 0)),
                                          (1, rgba(3, 5, 22, 0.6))))

    def do_snapshot(self, snap: Gtk.Snapshot) -> None:
        w, h = float(self.get_width()), float(self.get_height())
        if w < 2 or h < 2:
            return
        t = time.monotonic() - self.t0
        if self.art:
            self.draw_art(snap, w, h)
        else:
            self.draw_space(snap, w, h, t)

    def draw_space(self, snap: Gtk.Snapshot, w: float, h: float, t: float) -> None:
        """No picture found: the drawn sky."""
        full = rect(0, 0, w, h)
        snap.append_linear_gradient(full, point(0, 0), point(0, h),
                                    stops((0, rgba(3, 5, 22)), (0.55, rgba(9, 8, 42)), (1, rgba(32, 12, 54))))
        for fx, fy, size, (r, g, b, a) in ((0.12, 0.18, 0.60, (40, 110, 255, 0.24)),
                                           (0.88, 0.30, 0.65, (190, 40, 200, 0.22)),
                                           (0.50, 1.05, 0.75, (255, 120, 60, 0.20))):
            snap.append_radial_gradient(full, point(fx * w, fy * h), size * w, size * w * 0.75, 0, 1,
                                        stops((0, rgba(r, g, b, a)), (1, rgba(r, g, b, 0))))
        for x, y, size, phase, speed, (r, g, b) in self.stars:
            alpha = 0.35 + 0.65 * (0.5 + 0.5 * math.sin(t * speed + phase))
            snap.append_color(rgba(r, g, b, alpha), rect(x * w, y * h, size, size))
            if size == 3:
                snap.append_radial_gradient(rect(x * w - 8, y * h - 8, 16, 16), point(x * w + 1, y * h + 1), 8, 8, 0, 1,
                                            stops((0, rgba(r, g, b, 0.35 * alpha)), (1, rgba(r, g, b, 0))))

        # the planet, to the right, with its ring passing behind then in front
        radius = min(w, h) * 0.27
        cx, cy = w * 0.78, h * 0.62
        snap.append_radial_gradient(rect(cx - radius * 1.6, cy - radius * 1.6, radius * 3.2, radius * 3.2),
                                    point(cx, cy), radius * 1.6, radius * 1.6, 0, 1,
                                    stops((0, rgba(255, 150, 80, 0)), (0.62, rgba(255, 140, 80, 0)),
                                          (0.66, rgba(255, 160, 90, 0.35)), (1, rgba(255, 140, 80, 0))))
        self.ring(snap, cx, cy, radius, False)
        snap.push_rounded_clip(circle(cx, cy, radius))
        snap.append_radial_gradient(rect(cx - radius, cy - radius, 2 * radius, 2 * radius),
                                    point(cx - radius * 0.4, cy - radius * 0.45), radius * 1.8, radius * 1.8, 0, 1,
                                    stops((0, rgba(255, 200, 125)), (0.28, rgba(226, 104, 62)),
                                          (0.62, rgba(112, 40, 100)), (1, rgba(10, 5, 30))))
        for offset, height, alpha in ((0.18, 0.05, 0.10), (0.34, 0.09, 0.08), (0.52, 0.04, 0.12),
                                      (0.66, 0.08, 0.07)):
            snap.append_color(rgba(255, 230, 190, alpha),
                              rect(cx - radius, cy - radius + offset * 2 * radius, 2 * radius, height * 2 * radius))
        snap.pop()
        self.ring(snap, cx, cy, radius, True)
        mx, my, mr = cx - radius * 1.55, cy - radius * 1.05, radius * 0.16
        snap.push_rounded_clip(circle(mx, my, mr))
        snap.append_radial_gradient(rect(mx - mr, my - mr, 2 * mr, 2 * mr), point(mx - mr * 0.4, my - mr * 0.4),
                                    mr * 1.7, mr * 1.7, 0, 1,
                                    stops((0, rgba(230, 240, 255)), (0.5, rgba(120, 140, 200)),
                                          (1, rgba(12, 14, 40))))
        snap.pop()

        # a shooting star every 11 seconds
        step = (t % 11.0) / 1.1
        if step < 1.0:
            snap.save()
            snap.translate(point(w * (0.15 + 0.4 * ((int(t / 11.0) * 0.37) % 1)) + step * w * 0.35,
                                 h * 0.08 + step * h * 0.28))
            snap.rotate(34)
            snap.append_linear_gradient(rect(-150, 0, 150, 2), point(-150, 0), point(0, 0),
                                        stops((0, rgba(255, 255, 255, 0)), (1, rgba(255, 255, 255, 1 - step))))
            snap.restore()


# ---------------------------------------------------------------- the flags
# Drawn here (an emoji flag needs a font the Deck may not have): three of stripes, and the Union Jack.

FLAG_STRIPES = {  # code: (vertical?, colours, shares of each)
    "fr": (True, ((0, 85, 164), (255, 255, 255), (239, 65, 53)), (1, 1, 1)),
    "de": (False, ((0, 0, 0), (221, 0, 0), (255, 206, 0)), (1, 1, 1)),
    "it": (True, ((0, 146, 70), (255, 255, 255), (206, 43, 55)), (1, 1, 1)),
    "es": (False, ((170, 21, 27), (241, 191, 0), (170, 21, 27)), (1, 2, 1)),
}


class Flag(Gtk.Widget):
    __gtype_name__ = "RogueGalaxyFlag"

    def __init__(self) -> None:
        super().__init__(width_request=27, height_request=18, valign=Gtk.Align.CENTER)
        self.code = "en"

    def set_code(self, code: str) -> None:
        self.code = code
        self.queue_draw()

    def bar(self, snap: Gtk.Snapshot, angle: float, length: float, thick: float, color: Gdk.RGBA) -> None:
        """A bar through the flag's centre, turned by angle degrees."""
        snap.save()
        snap.rotate(angle)
        snap.append_color(color, rect(-length / 2, -thick / 2, length, thick))
        snap.restore()

    def do_snapshot(self, snap: Gtk.Snapshot) -> None:
        w, h = float(self.get_width()), float(self.get_height())
        if w < 2 or h < 2:
            return
        box = Gsk.RoundedRect()
        box.init_from_rect(rect(0, 0, w, h), 3)
        snap.push_rounded_clip(box)
        if self.code in FLAG_STRIPES:
            vertical, colors, shares = FLAG_STRIPES[self.code]
            total, at = float(sum(shares)), 0.0
            for (r, g, b), share in zip(colors, shares):
                size = (w if vertical else h) * share / total
                snap.append_color(rgba(r, g, b), rect(at, 0, size, h) if vertical else rect(0, at, w, size))
                at += size
        else:  # the Union Jack
            snap.append_color(rgba(1, 33, 105), rect(0, 0, w, h))
            snap.save()
            snap.translate(point(w / 2, h / 2))
            diagonal = math.degrees(math.atan2(h, w))
            long = math.hypot(w, h) * 1.2
            for angle in (diagonal, -diagonal):
                self.bar(snap, angle, long, h * 0.22, rgba(255, 255, 255))
                self.bar(snap, angle, long, h * 0.08, rgba(200, 16, 46))
            self.bar(snap, 0, w * 1.2, h * 0.36, rgba(255, 255, 255))
            self.bar(snap, 90, h * 1.2, h * 0.36, rgba(255, 255, 255))
            self.bar(snap, 0, w * 1.2, h * 0.2, rgba(200, 16, 46))
            self.bar(snap, 90, h * 1.2, h * 0.2, rgba(200, 16, 46))
            snap.restore()
        snap.pop()
        edge = Gsk.RoundedRect()
        edge.init_from_rect(rect(0, 0, w, h), 3)
        snap.append_border(edge, [1.0] * 4, [rgba(255, 255, 255, 0.35)] * 4)



# ---------------------------------------------------------------- the pictures of the tips
GOLD, SKY, CORAL, GREEN = rgba(244, 203, 82), rgba(127, 212, 255), rgba(255, 140, 122), rgba(112, 222, 142)
WHITE, NAVY, DEEP = rgba(242, 243, 252), rgba(40, 56, 140), rgba(8, 12, 42)


def box(snap: Gtk.Snapshot, color: Gdk.RGBA, x: float, y: float, w: float, h: float, r: float = 0.0) -> None:
    if r > 0:
        clip = Gsk.RoundedRect()
        clip.init_from_rect(rect(x, y, w, h), min(r, w / 2, h / 2))
        snap.push_rounded_clip(clip)
        snap.append_color(color, rect(x, y, w, h))
        snap.pop()
    else:
        snap.append_color(color, rect(x, y, w, h))


def disc(snap: Gtk.Snapshot, color: Gdk.RGBA, cx: float, cy: float, r: float) -> None:
    box(snap, color, cx - r, cy - r, 2 * r, 2 * r, r)


def ring(snap: Gtk.Snapshot, color: Gdk.RGBA, cx: float, cy: float, r: float, t: float) -> None:
    snap.append_border(circle(cx, cy, r), [t] * 4, [color] * 4)


def line(snap: Gtk.Snapshot, color: Gdk.RGBA, x1: float, y1: float, x2: float, y2: float, t: float) -> None:
    snap.save()
    snap.translate(point((x1 + x2) / 2, (y1 + y2) / 2))
    snap.rotate(math.degrees(math.atan2(y2 - y1, x2 - x1)))
    length = math.hypot(x2 - x1, y2 - y1)
    snap.append_color(color, rect(-length / 2, -t / 2, length, t))
    snap.restore()


class TipArt(Gtk.Widget):
    """One drawing per tip, in a 220 x 220 square: the Deck, the disc, the buttons the tip is about."""
    __gtype_name__ = "RogueGalaxyTipArt"

    def __init__(self) -> None:
        super().__init__(width_request=290, height_request=290, valign=Gtk.Align.CENTER)
        self.kind = "why"
        self.start = time.monotonic()
        self.add_tick_callback(lambda *_: self.queue_draw() or True)

    def set_kind(self, kind: str) -> None:
        self.kind = kind
        self.queue_draw()

    def text(self, snap: Gtk.Snapshot, s: str, x: float, y: float, size: float, color: Gdk.RGBA) -> None:
        layout = self.create_pango_layout(s)
        font = Pango.FontDescription.from_string("Sans Bold")
        font.set_absolute_size(size * Pango.SCALE)
        layout.set_font_description(font)
        w, h = layout.get_pixel_size()
        snap.save()
        snap.translate(point(x - w / 2, y - h / 2))
        snap.append_layout(layout, color)
        snap.restore()

    def deck(self, snap: Gtk.Snapshot, x: float, y: float, w: float, h: float) -> tuple[float, float, float, float]:
        """The Steam Deck seen from the front; returns its screen."""
        box(snap, rgba(24, 34, 96), x, y, w, h, h * 0.32)
        edge = Gsk.RoundedRect()
        edge.init_from_rect(rect(x, y, w, h), h * 0.32)
        snap.append_border(edge, [2.0] * 4, [rgba(127, 212, 255, 0.55)] * 4)
        sx, sy, sw, sh = x + w * 0.2, y + h * 0.1, w * 0.6, h * 0.8
        box(snap, DEEP, sx, sy, sw, sh, 5)
        disc(snap, SKY, x + w * 0.1, y + h * 0.34, h * 0.09)
        disc(snap, SKY, x + w * 0.1, y + h * 0.7, h * 0.07)
        disc(snap, GOLD, x + w * 0.9, y + h * 0.34, h * 0.06)
        disc(snap, CORAL, x + w * 0.9, y + h * 0.66, h * 0.06)
        return sx, sy, sw, sh

    def draw_kind(self, snap: Gtk.Snapshot) -> None:
        k = self.kind
        if k == "why":
            disc(snap, SKY, 110, 52, 34)
            disc(snap, DEEP, 110, 52, 9)
            ring(snap, WHITE, 110, 52, 34, 2)
            line(snap, GOLD, 110, 94, 110, 120, 7)
            line(snap, GOLD, 110, 124, 98, 110, 7)
            line(snap, GOLD, 110, 124, 122, 110, 7)
            sx, sy, sw, sh = self.deck(snap, 30, 132, 160, 76)
            box(snap, NAVY, sx + 8, sy + sh / 2 - 5, sw - 16, 10, 5)
            box(snap, GOLD, sx + 8, sy + sh / 2 - 5, (sw - 16) * 0.6, 10, 5)
        elif k == "menu":
            sx, sy, sw, sh = self.deck(snap, 14, 46, 192, 128)
            box(snap, rgba(30, 44, 120), sx + 6, sy + 6, sw - 12, sh - 12, 4)
            for i in range(4):
                box(snap, GOLD if i == 0 else SKY, sx + 12, sy + 14 + i * 16, 26, 7, 3)
            for i in range(3):
                box(snap, WHITE, sx + 48, sy + 16 + i * 20, sw - 66 - i * 14, 6, 3)
        elif k == "home":
            sx, sy, sw, sh = self.deck(snap, 14, 46, 192, 128)
            box(snap, GOLD, sx + sw / 2 - 28, sy + 8, 56, 8, 4)
            box(snap, GOLD, sx + sw / 2 - 34, sy + 26, 68, 18, 9)
            box(snap, SKY, sx + sw / 2 - 34, sy + 50, 68, 18, 9)
            box(snap, rgba(158, 168, 204), sx + sw / 2 - 34, sy + 74, 68, 18, 9)
        elif k == "states":
            for i in range(5):
                x = 12 + i * 41
                on = i == 2
                box(snap, GOLD if on else NAVY, x - 2, 38, 40, 58, 6)
                box(snap, SKY if on else rgba(70, 90, 170), x, 40, 36, 54, 5)
                box(snap, WHITE, x + 6, 70, 24, 14, 3)
                disc(snap, GOLD if not on else WHITE, x + 26, 54, 5)
            line(snap, GOLD, 110, 108, 110, 128, 6)
            line(snap, GOLD, 110, 132, 100, 120, 6)
            line(snap, GOLD, 110, 132, 120, 120, 6)
            box(snap, SKY, 76, 140, 68, 62, 8)
            box(snap, WHITE, 88, 146, 44, 26, 3)
            box(snap, DEEP, 94, 182, 32, 20, 3)
        elif k == "back":
            box(snap, rgba(24, 34, 96), 14, 70, 192, 112, 48)
            edge = Gsk.RoundedRect()
            edge.init_from_rect(rect(14, 70, 192, 112), 48)
            snap.append_border(edge, [2.0] * 4, [rgba(127, 212, 255, 0.55)] * 4)
            for label, x, y in (("L5", 28, 42), ("L4", 28, 78), ("R5", 152, 42), ("R4", 152, 78)):
                box(snap, GOLD, x, y, 40, 22, 11)
                self.text(snap, label, x + 20, y + 11, 15, DEEP)
            box(snap, DEEP, 78, 96, 64, 52, 6)
        elif k == "fps":
            for i, y in enumerate((74, 96, 118)):
                box(snap, SKY, 10 + i * 6, y, 34 - i * 6, 6, 3)
            self.text(snap, "60", 122, 96, 92, GOLD)
            self.text(snap, "FPS", 118, 166, 34, SKY)
        elif k == "options":
            for y, share in ((60, 0.3), (110, 0.75), (160, 0.5)):
                box(snap, NAVY, 20, y - 6, 180, 12, 6)
                box(snap, GOLD, 20, y - 6, 180 * share, 12, 6)
                disc(snap, WHITE, 20 + 180 * share, y, 14)
        elif k == "achievements":
            box(snap, GOLD, 70, 40, 80, 84, 36)
            ring(snap, GOLD, 64, 76, 20, 7)
            ring(snap, GOLD, 156, 76, 20, 7)
            box(snap, GOLD, 99, 120, 22, 34)
            box(snap, rgba(214, 160, 40), 72, 150, 76, 20, 8)
            self.text(snap, "130", 110, 80, 30, DEEP)
            for x, y in ((28, 34), (190, 40), (186, 150), (34, 150)):
                disc(snap, WHITE, x, y, 4)
        elif k == "languages":
            for i, (code, color) in enumerate((("EN", GOLD), ("FR", SKY), ("DE", CORAL), ("ES", GREEN), ("IT", WHITE))):
                x = 14 + (i % 3) * 66 if i < 3 else 47 + (i - 3) * 66
                y = 52 if i < 3 else 118
                box(snap, color, x, y, 58, 50, 16)
                self.text(snap, code, x + 29, y + 25, 24, DEEP)

    def do_snapshot(self, snap: Gtk.Snapshot) -> None:
        w, h = float(self.get_width()), float(self.get_height())
        if w < 2 or h < 2:
            return
        t = time.monotonic() - self.start
        scale = min(w, h) / 220
        snap.save()
        snap.translate(point((w - 220 * scale) / 2, (h - 220 * scale) / 2))
        snap.scale(scale, scale)
        snap.translate(point(0, math.sin(t * 1.6) * 4))
        self.draw_kind(snap)
        snap.restore()


class TipPage(Gtk.Box):
    """A picture on the left, a title and a few lines on the right."""

    def __init__(self) -> None:
        super().__init__(spacing=18)
        self.art = TipArt()
        column = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, valign=Gtk.Align.CENTER, hexpand=True)
        self.head = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=20)
        self.head.add_css_class("tip-head")
        self.body = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=26)
        self.body.add_css_class("tip")
        column.append(self.head)
        column.append(self.body)
        self.append(self.art)
        self.append(column)

    def show(self, key: str, body_key: str | None = None) -> None:
        self.art.set_kind(key.split("_", 1)[1])
        self.head.set_text(tr("tipt_" + key.split("_", 1)[1]))
        self.body.set_text(tr(body_key or key))


# ---------------------------------------------------------------- the window


class Window(Gtk.ApplicationWindow):
    """One page for everything to choose, then the same window shows the install."""

    def __init__(self, app: Gtk.Application, cfg: argparse.Namespace):
        super().__init__(application=app, title=tr("window_title"))
        self.cfg = cfg
        self.live_texts: dict = {}  # widget -> function giving its text, run again when the language changes
        self.custom_dir: Path | None = None
        self.job: Job | None = None
        self.pulse_id = 0
        self.tip_id = 0
        self.set_default_size(1100, 760)
        self.running = False
        self.connect("close-request", self.on_close_request)
        provider = Gtk.CssProvider()
        provider.load_from_data(CSS)
        Gtk.StyleContext.add_provider_for_display(self.get_display(), provider,
                                                  Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        self.stack = Gtk.Stack(transition_type=Gtk.StackTransitionType.CROSSFADE, vexpand=True, hexpand=True)
        self.stack.add_named(self.build_setup(), "setup")
        self.stack.add_named(self.build_install(), "install")
        root = Gtk.Overlay(child=Sky())
        root.add_overlay(self.stack)
        self.language_box = self.build_language_picker()
        root.add_overlay(self.language_box)
        self.set_child(root)
        self.live(self, lambda: tr("window_title"))
        self.retranslate()
        if cfg.shots:
            GLib.timeout_add(700, self.shoot, "setup")
            if cfg.tour:
                GLib.timeout_add(1500, self.tour_european)
                GLib.timeout_add(3400, self.tour_missing)
                GLib.timeout_add(5000, self.tour_install)
        if cfg.start or cfg.demo:
            GLib.idle_add(self.begin)
        if cfg.test_cancel:  # a test: cancel as the dialog's second button would
            GLib.timeout_add_seconds(cfg.test_cancel, lambda: self.cancel_install() or False)

    # -- the language: texts that follow it
    def live(self, widget, fn) -> None:
        """Gives the widget its text now and again each time the language changes; fn returns the text."""
        self.live_texts[widget] = fn
        self.apply_text(widget, fn())

    @staticmethod
    def apply_text(widget, text: str) -> None:
        if isinstance(widget, Gtk.Label):
            widget.set_text(text)
        elif isinstance(widget, Gtk.Window):
            widget.set_title(text)
        elif isinstance(widget, (Gtk.Entry, Gtk.PasswordEntry)):
            widget.set_property("placeholder-text", text)
        else:  # a button, a check button, an expander
            widget.set_label(text)

    def retranslate(self) -> None:
        for widget, fn in list(self.live_texts.items()):
            self.apply_text(widget, fn())
        self.language_drop.set_tooltip_text(tr("language"))
        self.show_tip()
        self.update_buttons()

    def build_language_picker(self) -> Gtk.Widget:
        """The list in the top right corner, at the Deck's language to begin with."""
        by_name = {name: code for code, name in LANGUAGES}
        factory = Gtk.SignalListItemFactory()  # a flag and the name, in the button and in the list

        def setup(_factory, item) -> None:
            row = Gtk.Box(spacing=10)
            row.append(Flag())
            row.append(Gtk.Label(xalign=0))
            item.set_child(row)

        def bind(_factory, item) -> None:
            name = item.get_item().get_string()
            flag = item.get_child().get_first_child()
            flag.set_code(by_name[name])
            flag.get_next_sibling().set_text(name)

        factory.connect("setup", setup)
        factory.connect("bind", bind)
        self.language_drop = Gtk.DropDown(model=Gtk.StringList.new([name for _, name in LANGUAGES]),
                                          selected=CODES.index(_current[0]), factory=factory)
        self.language_drop.connect("notify::selected", self.on_language)
        box = Gtk.Box(halign=Gtk.Align.END, valign=Gtk.Align.START, margin_top=14, margin_end=18)
        box.append(self.language_drop)
        return box

    def on_language(self, *_) -> None:
        code = CODES[self.language_drop.get_selected()]
        if code != _current[0]:
            set_language(code)
            self.retranslate()

    # -- building blocks
    def section(self, number: int, title: str, hint: str, action: Gtk.Widget | None = None) -> Gtk.Box:
        """A card with a numbered heading; the content is appended to it. Title and hint are keys of tr."""
        card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        card.add_css_class("card")
        head = Gtk.Box(spacing=12)
        badge = Gtk.Label(label=str(number), valign=Gtk.Align.CENTER)
        badge.add_css_class("badge")
        texts = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, hexpand=True)
        name = Gtk.Label(label="", xalign=0)
        name.add_css_class("section")
        self.live(name, lambda: tr(title))
        note = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=58)
        self.live(note, lambda: tr(hint))
        note.add_css_class("muted")
        card.note = note  # the hint, for a card whose hint says what was chosen
        texts.append(name)
        texts.append(note)
        head.append(badge)
        head.append(texts)
        if action:
            action.set_valign(Gtk.Align.CENTER)
            head.append(action)
        card.append(head)
        return card

    def header(self, text: str | None = None) -> Gtk.Box:
        """The title block; text is the key of its subtitle, if any."""
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        brand = Gtk.Label(label="ROGUE GALAXY", xalign=0)
        brand.add_css_class("brand")
        tagline = Gtk.Label(label="", xalign=0)
        self.live(tagline, lambda: tr("tagline"))
        tagline.add_css_class("tagline")
        box.append(brand)
        box.append(tagline)
        if text:
            sub = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=58, margin_top=4)
            self.live(sub, lambda: tr(text))
            sub.add_css_class("subtitle")
            box.append(sub)
        return box

    def beside_art(self, column: Gtk.Widget) -> Gtk.Box:
        """The column on the left at its own width; the rest of the window shows the picture."""
        page = Gtk.Box(vexpand=True)
        page.append(column)
        page.append(Gtk.Box(hexpand=True))
        return page

    tried = False  # Install was pressed with something missing

    PAGES = (("sec_disc", "disc_hint"), ("sec_where", "where_hint"), ("sec_extras", "extras_hint"),
             ("sec_ra", "ra_page_hint"), ("sec_recap", "recap_hint"))

    def build_setup(self) -> Gtk.Widget:
        """A wizard: one card at a time (disc, place, extras, achievements, recap), Back and Next below."""
        content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10,
                          margin_top=44, margin_bottom=2, margin_start=56, margin_end=24)
        steps = Gtk.Box(spacing=4)  # done, now, to come
        self.step_labels: list[Gtk.Label] = []
        for i, (title, _) in enumerate(self.PAGES):
            label = Gtk.Label(label="")
            self.live(label, lambda i=i, title=title: f"{i + 1}  {tr(title)}")
            label.add_css_class("step")
            steps.append(label)
            self.step_labels.append(label)
        content.append(steps)
        self.pages = Gtk.Stack(transition_type=Gtk.StackTransitionType.SLIDE_LEFT_RIGHT, vexpand=True,
                               vhomogeneous=False, interpolate_size=True)
        for i, build in enumerate((self.page_disc, self.page_where, self.page_extras, self.page_ra,
                                   self.page_recap)):
            holder = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, valign=Gtk.Align.START)
            holder.append(build())
            self.pages.add_named(holder, str(i))
        content.append(self.pages)
        self.page_index = 0

        bar = Gtk.Box(spacing=14, margin_top=4, margin_bottom=12, halign=Gtk.Align.CENTER)
        self.back_button = Gtk.Button()
        self.live(self.back_button, lambda: tr("back"))
        self.back_button.connect("clicked", lambda *_: self.show_page(self.page_index - 1))
        self.next_button = Gtk.Button()
        self.live(self.next_button, lambda: tr("next"))
        self.next_button.add_css_class("suggested-action")
        self.next_button.connect("clicked", lambda *_: self.on_next())
        self.install_button = Gtk.Button()
        self.live(self.install_button, lambda: tr("install"))
        self.install_button.add_css_class("suggested-action")
        self.install_button.connect("clicked", lambda *_: self.on_install())
        for button in (self.back_button, self.next_button, self.install_button):
            bar.append(button)
        left = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, hexpand=False, width_request=720)
        left.append(content)
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        page.append(self.beside_art(left))
        page.append(bar)
        self.show_page(0)
        return page

    def page_disc(self) -> Gtk.Widget:
        choose = Gtk.Button()
        self.live(choose, lambda: tr("choose_iso"))
        choose.connect("clicked", lambda *_: self.pick_file(self.set_iso, tr("iso_dialog")))
        card = self.section(1, "sec_disc", "disc_hint", choose)
        self.disc_card = card
        self.iso_label = card.note
        self.iso_state = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=58, visible=False)
        card.append(self.iso_state)
        if self.cfg.dir_ready:
            self.live(self.iso_label, lambda: tr("iso_extracted"))
        return card

    def page_where(self) -> Gtk.Widget:
        # 2. where
        other = Gtk.Button()
        self.live(other, lambda: tr("other_folder"))
        other.connect("clicked", lambda *_: self.pick_folder(self.set_folder))
        card = self.section(2, "sec_where", "where_hint", other)
        self.where_card = card
        self.where_buttons: list[tuple[Gtk.CheckButton, Path]] = []
        group = None
        for code, path in storage_choices():
            free = free_gb(path)
            row = Gtk.CheckButton()
            full = free < NEED_GB
            self.live(row, lambda code=code, path=path, free=free, full=full:
                      tr("where_row", label=storage_label(code), path=path, free=f"{free:.0f}")
                      + (("   " + tr("no_room")) if full else ""))
            if group is None:
                group = row
            else:
                row.set_group(group)
            if full:
                row.set_sensitive(False)
            card.append(row)
            self.where_buttons.append((row, path))
        usable = [(b, p) for b, p in self.where_buttons if b.get_sensitive()]
        if usable:  # the roomiest allowed one starts selected, the SD card over the internal storage
            usable[-1][0].set_active(True)
        for row, _ in self.where_buttons:
            row.connect("toggled", lambda *_: self.update_buttons())
        self.where_note = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=58, visible=False)
        card.append(self.where_note)
        return card

    def page_extras(self) -> Gtk.Widget:
        card = self.section(3, "sec_extras", "extras_hint")
        self.steam_check = Gtk.CheckButton()
        self.live(self.steam_check, lambda: tr("steam_check"))
        self.steam_check.set_active(self.cfg.steam)
        self.european_check = Gtk.CheckButton()
        self.live(self.european_check, lambda: tr("european_check"))
        self.european_check.set_active(bool(self.cfg.european))
        self.european_check.connect("toggled", lambda *_: self.on_european_toggled())
        row = Gtk.Box(spacing=12, margin_start=28)
        eu = Gtk.Button()
        self.live(eu, lambda: tr("european_choose"))
        eu.connect("clicked", lambda *_: self.pick_file(self.set_european, tr("european_dialog")))
        self.european_label = Gtk.Label(label="", xalign=0, hexpand=True, wrap=True, max_width_chars=58)
        self.live(self.european_label, self.european_text)
        self.european_label.add_css_class("muted")
        row.append(eu)
        row.append(self.european_label)
        self.european_reveal = Gtk.Revealer(transition_type=Gtk.RevealerTransitionType.SLIDE_DOWN,
                                            child=row, reveal_child=self.european_check.get_active())
        # The PCSX2 save: found by itself, else chosen by hand (a .ps2 card file or a folder card)
        self.saves_check = Gtk.CheckButton()
        self.live(self.saves_check, lambda: tr("saves_check"))
        self.saves_check.set_active(bool(self.cfg.saves))
        self.saves_check.connect("toggled", lambda *_: self.on_saves_toggled())
        self.saves_problem = None if self.cfg.saves else "saves_none"
        self.saves_names = list_saves(self.cfg.saves) if self.cfg.saves else []
        pick_file = Gtk.Button()
        self.live(pick_file, lambda: tr("saves_file"))
        pick_file.connect("clicked", lambda *_: self.pick_file(self.set_saves, tr("saves_file_dialog")))
        pick_dir = Gtk.Button()
        self.live(pick_dir, lambda: tr("saves_folder"))
        pick_dir.connect("clicked", lambda *_: self.pick_folder(self.set_saves, tr("saves_folder_dialog")))
        buttons = Gtk.Box(spacing=12)
        buttons.append(pick_file)
        buttons.append(pick_dir)
        self.saves_label = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=58)
        self.live(self.saves_label, self.saves_text)
        self.saves_label.add_css_class("muted")
        saves_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4, margin_start=28)
        saves_box.append(self.saves_label)
        saves_box.append(buttons)
        self.saves_reveal = Gtk.Revealer(transition_type=Gtk.RevealerTransitionType.SLIDE_DOWN,
                                         child=saves_box, reveal_child=self.saves_check.get_active())
        card.append(self.steam_check)
        card.append(self.european_check)
        card.append(self.european_reveal)
        card.append(self.saves_check)
        card.append(self.saves_reveal)
        return card

    def page_ra(self) -> Gtk.Widget:
        card = self.section(4, "sec_ra", "ra_page_hint")
        # The achievements: ticking it shows the sign-in fields (RetroAchievements account).
        self.ra_done = RA_LOGIN.exists()
        self.ra_check = Gtk.CheckButton()
        self.live(self.ra_check, lambda: tr("ra_check"))
        self.ra_check.set_active(self.ra_done)
        self.ra_check.connect("toggled", lambda *_: self.on_ra_toggled())
        self.ra_user = Gtk.Entry(hexpand=True)
        self.live(self.ra_user, lambda: tr("ra_user"))
        self.ra_password = Gtk.PasswordEntry(show_peek_icon=True, hexpand=True)
        self.live(self.ra_password, lambda: tr("ra_password"))
        self.ra_button = Gtk.Button()
        self.live(self.ra_button, lambda: tr("ra_signin"))
        self.ra_button.connect("clicked", lambda *_: self.on_ra_sign_in())
        self.ra_password.connect("activate", lambda *_: self.on_ra_sign_in())
        row = Gtk.Box(spacing=12)
        row.append(self.ra_user)
        row.append(self.ra_password)
        row.append(self.ra_button)
        self.ra_state = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=58)
        self.live(self.ra_state, lambda: tr("ra_done" if self.ra_done else "ra_hint"))
        self.ra_state.add_css_class("good" if self.ra_done else "muted")
        ra_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4, margin_start=28)
        ra_box.append(row)
        ra_box.append(self.ra_state)
        self.ra_reveal = Gtk.Revealer(transition_type=Gtk.RevealerTransitionType.SLIDE_DOWN,
                                      child=ra_box, reveal_child=self.ra_check.get_active())
        card.append(self.ra_check)
        card.append(self.ra_reveal)
        return card

    def page_recap(self) -> Gtk.Widget:
        card = self.section(5, "sec_recap", "recap_hint")
        self.recap = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=64)
        self.recap.add_css_class("recap")
        card.append(self.recap)
        return card

    # -- the pages
    def page_of(self, widget: Gtk.Widget) -> int:
        """The page a problem is on."""
        if widget is self.disc_card:
            return 0
        if widget is self.where_card:
            return 1
        if widget in (self.european_check, self.european_label, self.saves_check, self.saves_label):
            return 2
        return 3

    def show_page(self, index: int) -> None:
        self.page_index = max(0, min(len(self.PAGES) - 1, index))
        self.pages.set_visible_child_name(str(self.page_index))
        last = self.page_index == len(self.PAGES) - 1
        self.back_button.set_visible(self.page_index > 0)
        self.next_button.set_visible(not last)
        self.install_button.set_visible(last)
        for i, label in enumerate(self.step_labels):
            for name in ("step-on", "step-done"):
                label.remove_css_class(name)
            if i == self.page_index:
                label.add_css_class("step-on")
            elif i < self.page_index:
                label.add_css_class("step-done")
        self.update_recap()

    def on_next(self) -> None:
        """On to the next page, unless what this page asks is not filled in: it shows in red and stays."""
        found = [w for w in self.problems() if self.page_of(w) == self.page_index]
        self.tried = True
        self.mark(self.problems())
        if not found:
            self.show_page(self.page_index + 1)

    def update_recap(self) -> None:
        yes, no = tr("recap_yes"), tr("recap_no")
        folder = self.chosen_dir() or self.cfg.dir
        disc = tr("recap_extracted") if self.cfg.dir_ready and self.cfg.iso is None else (
            self.cfg.iso.name if self.cfg.iso else "-")
        european = self.cfg.european.name if self.european_check.get_active() and self.cfg.european else no
        self.recap.set_text("\n".join((
            tr("recap_disc", value=disc),
            tr("recap_where", value=folder if folder else "-"),
            tr("recap_steam", value=yes if self.steam_check.get_active() else no),
            tr("recap_european", value=european),
            tr("recap_saves", value=yes if self.saves_check.get_active() and self.cfg.saves else no),
            tr("recap_ra", value=yes if self.ra_check.get_active() and self.ra_done else no))))

    def build_install(self) -> Gtk.Widget:
        content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, width_request=720, vexpand=True,
                          hexpand=False, margin_top=22, margin_bottom=18, margin_start=56, margin_end=24)
        content.append(self.header())
        self.install_title = Gtk.Label(label="", xalign=0, margin_top=14)
        self.live(self.install_title, lambda: tr("installing"))
        self.install_title.add_css_class("title")
        self.install_sub = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=58)
        self.live(self.install_sub, lambda: tr("install_sub"))
        self.install_sub.add_css_class("subtitle")
        content.append(self.install_title)
        content.append(self.install_sub)
        card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        card.add_css_class("card")
        self.step_label = Gtk.Label(label="", xalign=0)
        self.live(self.step_label, lambda: tr("starting"))
        self.bar = Gtk.ProgressBar()
        self.fail_label = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=58, visible=False)
        self.fail_label.add_css_class("bad")
        self.done_label = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=58, visible=False)
        card.append(self.step_label)
        card.append(self.bar)
        card.append(self.fail_label)
        card.append(self.done_label)
        content.append(card)
        content.append(self.build_tips())
        expander = Gtk.Expander()
        self.live(expander, lambda: tr("details"))
        self.log_view = Gtk.TextView(editable=False, monospace=True, wrap_mode=Gtk.WrapMode.CHAR)
        self.log_scroll = Gtk.ScrolledWindow(min_content_height=200, child=self.log_view)
        expander.set_child(self.log_scroll)
        content.append(expander)
        self.close_button = Gtk.Button(halign=Gtk.Align.CENTER, visible=False, margin_bottom=16)
        self.live(self.close_button, lambda: tr("close"))
        self.close_button.add_css_class("suggested-action")
        self.close_button.connect("clicked", lambda *_: self.get_application().quit())
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        page.append(self.beside_art(content))
        page.append(self.close_button)  # centred in the window, like Install
        return page

    # -- the tips under the bar, one after the other while it builds
    def build_tips(self) -> Gtk.Widget:
        self.tips_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8, vexpand=True,
                                valign=Gtk.Align.CENTER, margin_top=6)
        self.tips_box.add_css_class("card")
        head = Gtk.Box(spacing=8)
        title = Gtk.Label(label="", xalign=0, hexpand=True)
        title.add_css_class("tip-title")
        self.live(title, lambda: tr("tips_title").upper())
        self.tip_count = Gtk.Label(label="", xalign=1)
        self.tip_count.add_css_class("tip-count")
        head.append(title)
        head.append(self.tip_count)
        self.tip_stack = Gtk.Stack(transition_type=Gtk.StackTransitionType.CROSSFADE, transition_duration=600,
                                   hhomogeneous=True, vhomogeneous=True)
        self.tip_pages = (TipPage(), TipPage())
        self.tip_stack.add_named(self.tip_pages[0], "0")
        self.tip_stack.add_named(self.tip_pages[1], "1")
        self.tips_box.append(head)
        self.tips_box.append(self.tip_stack)
        self.tip_index = 0
        self.tip_page = 0
        self.show_tip()
        self.tip_id = GLib.timeout_add_seconds(TIP_SECONDS, self.next_tip)
        return self.tips_box

    def show_tip(self) -> None:
        """The tip now on show, again (a new language)."""
        self.tip_pages[self.tip_page].show(TIPS[self.tip_index], self.tip_body(TIPS[self.tip_index]))
        self.tip_count.set_text(f"{self.tip_index + 1} / {len(TIPS)}")

    def tip_body(self, key: str) -> str:
        """The text of a tip: the achievements' one says so when an account is connected (they start on)."""
        return "tip_achievements_on" if key == "tip_achievements" and self.ra_done else key

    def next_tip(self) -> bool:
        self.tip_index = (self.tip_index + 1) % len(TIPS)
        self.tip_page = 1 - self.tip_page
        self.tip_pages[self.tip_page].show(TIPS[self.tip_index], self.tip_body(TIPS[self.tip_index]))
        self.tip_stack.set_visible_child_name(str(self.tip_page))
        self.tip_count.set_text(f"{self.tip_index + 1} / {len(TIPS)}")
        return True

    def stop_tips(self) -> None:
        if self.tip_id:
            GLib.source_remove(self.tip_id)
            self.tip_id = 0
        self.tips_box.set_visible(False)

    # -- RetroAchievements
    def on_ra_sign_in(self) -> None:
        user, password = self.ra_user.get_text().strip(), self.ra_password.get_text()
        if not user or not password:
            self.show_ra(lambda: tr("ra_need"), False)
            return
        self.ra_button.set_sensitive(False)
        self.show_ra(lambda: tr("ra_signing"), True)

        def work() -> None:
            done, message = ra_sign_in(user, password)
            GLib.idle_add(self.on_ra_answer, done, message)

        threading.Thread(target=work, daemon=True).start()

    def on_ra_toggled(self) -> None:
        self.ra_reveal.set_reveal_child(self.ra_check.get_active())
        self.update_buttons()

    def on_ra_answer(self, done: bool, message) -> bool:
        """message: a function giving the text to show, in the window's language."""
        self.ra_button.set_sensitive(True)
        text = message
        if done:
            self.ra_done = True
            self.ra_password.set_text("")  # not kept anywhere
            text = lambda: message() + " " + tr("ra_auto")  # noqa: E731
        self.show_ra(text, done)
        self.update_buttons()
        return False

    def show_ra(self, text, good: bool) -> None:
        """text: a function giving the line to show."""
        self.live(self.ra_state, text)
        for name in ("good", "bad", "muted"):
            self.ra_state.remove_css_class(name)
        self.ra_state.add_css_class("good" if good else "bad")

    # -- choosing files
    def pick_file(self, callback, title: str) -> None:
        dialog = Gtk.FileDialog(title=title)
        dialog.open(self, None, lambda d, r: self.picked(d, r, callback, False))

    def pick_folder(self, callback, title: str | None = None) -> None:
        dialog = Gtk.FileDialog(title=title or tr("folder_dialog"))
        dialog.select_folder(self, None, lambda d, r: self.picked(d, r, callback, True))

    def picked(self, dialog, result, callback, folder: bool) -> None:
        try:
            file = dialog.select_folder_finish(result) if folder else dialog.open_finish(result)
        except GLib.Error:
            return
        if file and file.get_path():
            callback(Path(file.get_path()))

    def set_iso(self, path: Path) -> None:
        problem = looks_like_iso(path)  # None, or the key of what is wrong
        self.cfg.iso = None if problem else path
        self.live(self.iso_label, lambda: str(path))
        self.live(self.iso_state, lambda: tr(problem or "iso_ok"))
        self.iso_state.set_visible(True)
        for cls in ("good", "bad"):
            self.iso_state.remove_css_class(cls)
        self.iso_state.add_css_class("bad" if problem else "good")
        self.update_buttons()

    def set_folder(self, path: Path) -> None:
        self.custom_dir = path / "RogueGalaxy" if any(path.iterdir()) else path
        free = free_gb(self.custom_dir)
        for row, _ in self.where_buttons:
            row.set_active(False)
        enough = free >= NEED_GB
        self.where_note.set_visible(True)
        folder = self.custom_dir
        self.live(self.where_note, lambda: tr("where_note" if enough else "where_note_short",
                                              path=folder, free=f"{free:.0f}"))
        self.update_buttons()

    def european_text(self) -> str:
        return self.cfg.european.name if self.cfg.european else tr("european_none")

    def set_european(self, path: Path) -> None:
        self.cfg.european = path
        self.apply_text(self.european_label, self.european_text())
        self.update_buttons()

    def on_european_toggled(self) -> None:
        self.european_reveal.set_reveal_child(self.european_check.get_active())
        self.update_buttons()

    def saves_text(self) -> str:
        if self.cfg.saves:
            where = self.cfg.saves.parent
            return tr("saves_found" if len(self.saves_names) != 1 else "saves_found_one", name=self.cfg.saves.name, n=len(self.saves_names), where=where)
        return tr(self.saves_problem or "saves_none")

    def set_saves(self, path: Path) -> None:
        names = list_saves(path)
        self.cfg.saves = path if names else None
        self.saves_names = names
        self.saves_problem = None if names else "saves_bad"
        self.live(self.saves_label, self.saves_text)
        for name in ("good", "bad", "muted"):
            self.saves_label.remove_css_class(name)
        self.saves_label.add_css_class("good" if names else "bad")
        self.saves_check.set_active(True)  # a bad pick keeps the line, and its reason, in view
        self.update_buttons()

    def on_saves_toggled(self) -> None:
        self.saves_reveal.set_reveal_child(self.saves_check.get_active())
        self.update_buttons()

    # -- what is chosen
    def chosen_dir(self) -> Path | None:
        if self.custom_dir and not any(b.get_active() for b, _ in self.where_buttons):
            return self.custom_dir if free_gb(self.custom_dir) >= NEED_GB else None
        for row, path in self.where_buttons:
            if row.get_active():
                return path
        return None

    def problems(self) -> list[Gtk.Widget]:
        """What is not filled in yet: the cards and options to show in red."""
        found = []
        if self.cfg.iso is None and not self.cfg.dir_ready:
            found.append(self.disc_card)
        if self.chosen_dir() is None and not self.cfg.dir:
            found.append(self.where_card)
        if self.european_check.get_active() and self.cfg.european is None:
            found += [self.european_check, self.european_label]
        if self.saves_check.get_active() and not self.cfg.saves:
            found += [self.saves_check, self.saves_label]
        if self.ra_check.get_active() and not self.ra_done:
            found += [self.ra_check, self.ra_state]
        return found

    def update_buttons(self) -> None:
        """After Install was pressed once with something missing, the red marks follow what is filled in."""
        if self.tried:
            self.mark(self.problems())
        if hasattr(self, "recap"):
            self.update_recap()

    def mark(self, found: list[Gtk.Widget]) -> None:
        for widget in (self.disc_card, self.where_card, self.european_check, self.european_label,
                       self.saves_check, self.saves_label, self.ra_check, self.ra_state):
            on = widget in found
            for name in (("invalid",) if widget in (self.disc_card, self.where_card) else ("invalid-text",)):
                (widget.add_css_class if on else widget.remove_css_class)(name)

    def on_install(self) -> None:
        found = self.problems()
        self.tried = True
        self.mark(found)
        if found:
            self.show_page(min(self.page_of(w) for w in found))
        else:
            self.begin()

    def begin(self) -> None:
        self.cfg.dir = self.cfg.dir or self.chosen_dir()
        if not self.cfg.start:
            self.cfg.steam = self.steam_check.get_active()
        if not self.european_check.get_active():
            self.cfg.european = None
        if not self.saves_check.get_active():
            self.cfg.saves = None
        self.stack.set_visible_child_name("install")
        self.language_box.set_visible(False)  # the progress lines are written in the language chosen now
        self.pulse_id = GLib.timeout_add(120, self.pulse)
        self.job = Job(self.cfg, self)
        self.running = True
        if not self.cfg.demo:
            self.awake = KeepAwake()
            self.awake.start("Installing Rogue Galaxy")
        self.job.start()

    # -- closing the window while it builds: ask, then really stop
    def on_close_request(self, *_) -> bool:
        if not self.running or self.cancelling:
            return self.cancelling  # not building: close; being cancelled: wait for the end
        job = self.job
        where = job.cfg.dir if job else None
        kept = tr("kept_fresh", folder=where) if job and job.fresh else tr("kept_old")
        dialog = Gtk.AlertDialog(message=tr("cancel_q"),
                                 detail=tr("cancel_detail", kept=kept),
                                 buttons=[tr("keep_installing"), tr("cancel_install")],
                                 cancel_button=0, default_button=0, modal=True)
        dialog.choose(self, None, self.on_close_answer)
        return True

    def on_close_answer(self, dialog, result) -> None:
        try:
            choice = dialog.choose_finish(result)
        except GLib.Error:
            return
        if choice == 1:
            self.cancel_install()

    cancelling = False
    awake: KeepAwake | None = None

    def release_awake(self) -> None:
        if self.awake:
            self.awake.stop()
            self.awake = None

    def cancel_install(self) -> None:
        if not self.running or self.cancelling or not self.job:
            return
        self.cancelling = True
        self.step_label.set_text(tr("cancelling"))
        self.pulsing = True
        threading.Thread(target=self.job.cancel, daemon=True).start()

    def on_cancelled(self) -> bool:
        self.running = False
        self.release_awake()
        self.get_application().quit()
        return False

    # -- the job's callbacks (on the main thread)
    pulsing = True

    def pulse(self) -> bool:
        if self.pulsing:
            self.bar.pulse()
        return True

    def on_progress(self, text: str, fraction: float | None) -> bool:
        self.step_label.set_text(text)
        if fraction is None:
            self.pulsing = True
        else:
            self.pulsing = False
            self.bar.set_fraction(max(0.0, min(1.0, fraction)))
        return False

    def on_log(self, line: str) -> bool:
        buffer = self.log_view.get_buffer()
        buffer.insert(buffer.get_end_iter(), line + "\n")
        if buffer.get_line_count() > 2000:
            buffer.delete(buffer.get_start_iter(), buffer.get_iter_at_line(500))
        adjustment = self.log_scroll.get_vadjustment()
        adjustment.set_value(adjustment.get_upper())
        return False

    def on_failed(self, message: str) -> bool:
        self.running = False
        self.release_awake()
        self.pulsing = False
        self.stop_tips()
        self.fail_label.set_text(message)
        self.fail_label.set_visible(True)
        self.step_label.set_text(tr("stopped"))
        self.close_button.set_visible(True)
        if self.cfg.shots:
            GLib.timeout_add(500, self.shoot, "failed")
        return False

    def on_done(self) -> bool:
        self.running = False
        self.release_awake()
        self.pulsing = False
        self.stop_tips()
        self.bar.set_fraction(1.0)
        self.install_title.set_text(tr("done"))
        self.install_sub.set_text(tr("done_sub"))
        self.step_label.set_visible(False)
        steam = tr("done_steam" if self.cfg.steam else "done_nosteam")
        self.done_label.set_text(steam + " " + tr("done_tail"))
        self.done_label.set_visible(True)
        self.close_button.set_visible(True)
        return False

    # -- a PNG of the window, for checking the look without a screen
    def tour_european(self) -> bool:
        self.european_check.set_active(True)  # both options open
        self.ra_check.set_active(True)
        for i in range(len(self.PAGES)):
            GLib.timeout_add(i * 350, lambda i=i: self.show_page(i) or False)
            GLib.timeout_add(250 + i * 350, self.shoot, f"setup-page{i + 1}")
        return False

    def tour_missing(self) -> bool:
        self.on_install()  # nothing is filled in: the red marks
        GLib.timeout_add(500, self.shoot, "setup-missing")
        return False

    def tour_install(self) -> bool:
        self.stack.set_visible_child_name("install")
        GLib.timeout_add(500, self.shoot, "install")
        return False

    def shoot(self, name: str) -> bool:
        from gi.repository import Graphene  # noqa: PLC0415

        paintable = Gtk.WidgetPaintable.new(self)
        width, height = self.get_width(), self.get_height()
        snapshot = Gtk.Snapshot()
        paintable.snapshot(snapshot, width, height)
        node = snapshot.to_node()
        renderer = self.get_native().get_renderer()
        if node and renderer:
            texture = renderer.render_texture(node, Graphene.Rect().init(0, 0, width, height))
            Path(self.cfg.shots).mkdir(parents=True, exist_ok=True)
            texture.save_to_png(str(Path(self.cfg.shots) / f"{name}.png"))
        return False


class App(Gtk.Application):
    def __init__(self, cfg: argparse.Namespace):
        super().__init__(application_id="io.github.deckgalaxy.Setup", flags=Gio.ApplicationFlags.NON_UNIQUE)
        self.cfg = cfg

    def do_activate(self) -> None:
        Window(self, self.cfg).present()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--iso", type=Path)
    ap.add_argument("--dir", type=Path)
    ap.add_argument("--zip", type=Path)
    ap.add_argument("--japanese", type=Path)
    ap.add_argument("--european", type=Path)
    ap.add_argument("--saves", type=Path, help="a PCSX2 memory card (.ps2 file or folder) to import; "
                    "else the Deck is searched for one")
    ap.add_argument("--no-steam", dest="steam", action="store_false")
    ap.add_argument("--demo", action="store_true", help="show the install page with a made-up build (nothing is installed)")
    ap.add_argument("--start", action="store_true", help="skip the pages and install now (needs --dir)")
    ap.add_argument("--lang", choices=CODES, help="the window's language (else the Deck's, or RG_LANG)")
    ap.add_argument("--shots", help="save a PNG of each page in this folder")
    ap.add_argument("--tour", action="store_true", help="with --shots, show each page in turn")
    ap.add_argument("--test-cancel", type=int, default=0, metavar="SECONDS",
                    help="cancel the install after this many seconds (a test)")
    cfg = ap.parse_args()
    if cfg.start and not cfg.dir:
        ap.error("--start needs --dir")
    set_language(cfg.lang or detect_language())
    if cfg.saves and not list_saves(cfg.saves):
        ap.error(f"no Rogue Galaxy save in {cfg.saves}")
    if cfg.saves is None and not cfg.start:
        cards = find_cards()  # PCSX2's own folders: the most recent card with a save
        cfg.saves = cards[0] if cards else None
    # An already extracted disc in the folder needs no image.
    cfg.dir_ready = bool(cfg.dir and (cfg.dir / "disc" / "manifest.json").exists())
    return App(cfg).run([sys.argv[0]])


if __name__ == "__main__":
    sys.exit(main())
