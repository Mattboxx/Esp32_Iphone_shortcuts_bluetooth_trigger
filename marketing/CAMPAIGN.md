# Campagna di lancio: ESP32 BT Trigger v2

Link da usare ovunque: **https://mattboxx.github.io/esp32-bt-trigger/**
(il sito, non il repo: il sito converte, il repo spaventa chi non è sviluppatore).

---

## 0. Prima di pubblicare qualsiasi cosa (bloccante)

Se salti questi punti bruci il lancio. Su Reddit e HN il primo post è anche l'ultimo.

1. **Testa la v2 su hardware vero.** Su un ESP32 classico + iPhone: pairing, automazione,
   riavvio → riconnessione, modalità *switch* e *pulse*, "Save settings only", pulsante
   reset 5 s. Il codice compila ma non è mai stato acceso. Un commento "non funziona"
   nelle prime due ore uccide il thread.
2. **Compra una ESP32-C3 SuperMini (~3 €) e prova la variante BLE.** Se funziona, il
   messaggio diventa "qualsiasi ESP32 da 3 €", che è molto più forte. Se non funziona,
   lascia il badge *beta* e non nominarla nei post.
3. **Registra un video di 15-20 secondi. È l'asset più importante di tutta la campagna.**
   Inquadratura unica: mano che accende il monopattino/lampada → l'iPhone sul manubrio
   apre Maps da solo. Niente musica, niente intro. Senza video questo progetto è "un
   altro firmware ESP32", con il video diventa "ah, lo voglio".
4. Abilita GitHub Pages: *Settings → Pages → Source: GitHub Actions*.
5. Crea il tag `v2.0.0` (genera la Release con i binari) e aggiungi al repo:
   description = la tagline sotto, topics = `esp32 iphone shortcuts bluetooth hid ios-automation esp-idf home-assistant`, website = link del sito.

## 0.b Un firmware, due messaggi

- **All'accensione**: "quando si accende, l'iPhone fa qualcosa" (monopattino, auto, scrivania).
- **Da pin**: "un contatto fisico diventa un'automazione iPhone" (porta, bottone, relè).
  Link diretti che preselezionano la modalità: `…/?mode=switch` (interruttore) e `…/?mode=button` (pulsante).

Non mischiarli nello stesso post: un post = un caso d'uso = un link. Il messaggio "pin"
rende di più su r/homeassistant e r/esp32, quello "accensione" su r/shortcuts e sui subreddit veicoli.

## 0.d Prossimo passo dopo i test: Comandi Rapidi pronti

Pubblica 3 Comandi Rapidi condivisibili (link iCloud) per i casi principali:
monopattino → apri Maps, scrivania → Concentrazione, porta → notifica / Home Assistant.
Mettili sul sito nel pannello "Done!". Abbassano la barriera più di qualsiasi funzione nuova.

## 0.c Donazioni (Ko-fi + GitHub Sponsors)

Regola: **mai chiedere nei post di lancio.** Il link alle donazioni vive sul sito, dove
compare *dopo* che l'utente ha ottenuto valore (pannello "Done!" a flash riuscito) e in
una sezione discreta in fondo. Chi dona è chi ha già un trigger funzionante. Nei commenti,
se qualcuno chiede "come posso ringraziarti?", rispondi con il link: è l'unico momento
in cui nominarlo non costa reputazione.

Prerequisito: attiva GitHub Sponsors sul profilo (github.com/sponsors), altrimenti il
link `github.com/sponsors/Mattboxx` porta a una pagina vuota. Ko-fi è già attivo.

## 1. Posizionamento

**Per chi:** utenti iPhone che usano Comandi Rapidi e vogliono che il telefono reagisca
a qualcosa del mondo fisico. Sono maker leggeri, persone di Home Assistant, chi ha un
monopattino o un'e-bike con il telefono sul manubrio, chi gira con un'auto senza CarPlay.

**Il problema reale:** iOS può lanciare automazioni quando si connette un dispositivo
Bluetooth, ma serve un dispositivo che l'iPhone accetti di accoppiare. Un beacon BLE non
compare nemmeno nella lista. Le alternative hanno tutte un difetto:

| Alternativa | Perché perde |
|---|---|
| Tag NFC | va toccato a mano, ogni volta |
| Bottoni BT commerciali (Flic ecc.) | 30-40 €, app proprietaria, batteria |
| Usare l'autoradio o le cuffie come trigger | funziona solo se quel dispositivo ce l'hai già |
| Geofence / orari | impreciso, non sa se il motore è acceso |

**Tagline:** *When it turns on, your iPhone runs a Shortcut.*
**Una riga:** *A $3 ESP32 that your iPhone sees as a Bluetooth accessory: power it on and your Shortcuts automation runs. Flash it from the browser, no code.*

**Non dire mai:** "funziona con tutto", "zero consumo", "testato su tutte le schede".
Il pubblico maker fa fact-checking in tempo reale.

## 2. Canali, in ordine di rendimento atteso

| # | Canale | Perché | Formato |
|---|---|---|---|
| 1 | r/shortcuts | esattamente il target, ~300k utenti | video + testo breve |
| 2 | r/esp32 | maker, ti danno feedback tecnico e stelle | testo tecnico + video |
| 3 | r/homeassistant | enorme, ama i trigger locali senza cloud | angolo "trigger locale per HA" |
| 4 | Show HN | traffico alto ma volatile, solo se il sito è solido | titolo sobrio |
| 5 | Hackaday tips (tips@hackaday.com) | un articolo = settimane di traffico | 3 righe + link |
| 6 | r/ElectricScooters, r/ebikes, r/3Dprinting | casi d'uso verticali | solo il loro caso d'uso |
| 7 | YouTube Shorts / TikTok / Reels | il video del punto 0.3 | 15 s, testo in sovraimpressione |
| 8 | Gruppi italiani (Telegram/Facebook "Comandi Rapidi", forum HA Italia) | community piccola ma calda | post in italiano |

**Cadenza:** un canale al giorno, non tutti insieme. Inizia da r/esp32 (pubblico più
indulgente, ti trova i bug), poi r/shortcuts con i bug sistemati, poi gli altri.
Rispondi a **ogni** commento nelle prime 3 ore: è lì che si decide il ranking.

## 3. Post pronti

### r/shortcuts
**Title:** I made a $3 Bluetooth trigger for Shortcuts: when it powers on, my iPhone runs an automation

> [video]
>
> The Bluetooth "Is Connected" automation is great, but you need a device the iPhone will actually pair with. Beacons don't show up. So I wrote a tiny firmware that makes an ESP32 board look like a Bluetooth mouse that never moves.
>
> Power it from whatever you want to detect (scooter, car, desk lamp, 3D printer). It connects, Shortcuts runs.
>
> - flash it from Chrome in ~2 minutes, no code: [link]
> - no app, no cloud, no Wi-Fi, reconnects by itself
> - also works with a switch (door reed switch) or a push button
> - open source (MIT)
>
> Happy to answer questions. What would you trigger with it?

### r/esp32
**Title:** Classic BT HID device on ESP-IDF as an iPhone Shortcuts trigger, plus a Web Serial installer that writes settings to a custom partition

> The ESP32 exposes itself as a Classic BT HID mouse (no reports ever sent). iOS lets Shortcuts run on "device connected", so connecting *is* the event. Some details that might be useful to others:
>
> - The BT HID Device role is off in the precompiled Arduino libs, so this is plain ESP-IDF 5.3.
> - With Classic HID the *device* must reconnect after a reboot: it calls back the last connected bonded phone, then round-robins the others.
> - Settings (name, trigger mode, pins, LED) live in a 4 KB `btcfg` partition. The web installer (esptool-js) writes firmware + that sector, reads it back to pre-fill the form, and can update settings only, without touching NVS, so pairings survive.
> - Same 2 MB image for every ESP32 board; UNICORE + auto-crystal so SOLO/26 MHz boards boot.
> - C3/S3/C6 get a BLE HID-over-GATT build. **Beta, not confirmed on iPhone yet**: if you have a C3 + iPhone, I'd love a test report.
>
> Code + installer: [link]

### r/homeassistant
**Title:** A local, cloud-free physical trigger for iPhone → Home Assistant: $3 ESP32 that fires a Shortcut when it powers on

> Shortcuts can call HA (scripts, webhooks, scenes). The missing piece was a cheap physical event the iPhone reacts to without an app or tapping an NFC tag. This firmware makes an ESP32 a Bluetooth accessory: power on (or switch closes, or button press) → iPhone connects → your automation runs → HA. Web installer, no YAML, no code: [link]

### Show HN
**Title:** Show HN: Turn a $3 ESP32 into a Bluetooth trigger for iPhone Shortcuts

> First comment: why a mouse (iOS only pairs certain device classes from Settings; a keyboard hides the on-screen keyboard), why Classic BT, how the settings partition + browser installer works, what is still untested (BLE on C3/S3/C6).

### Hackaday tip
> Subject: ESP32 as a Bluetooth trigger for iPhone Shortcuts (browser installer)
> An ESP32 pretends to be a Bluetooth mouse so that powering it on fires an iOS Shortcuts automation: scooter on → Maps opens, desk lamp on → Focus mode. Web Serial installer writes firmware + settings to a custom partition. [link]

### Italiano (gruppi Telegram / Facebook)
> Ho fatto un trigger Bluetooth da 3 € per i Comandi Rapidi: quando la scheda si accende, l'iPhone esegue l'automazione da solo. Niente app, niente cloud, niente NFC da toccare. Si installa dal browser (Chrome) in 2 minuti, senza scrivere codice. Esempi: accendi il monopattino → si apre Maps; accendi la lampada della scrivania → modalità Concentrazione. Open source: [link]

### Video (15-20 s, verticale)
1. 0-3 s: testo "My iPhone knows when I turn on my scooter". Mano sul tasto di accensione.
2. 3-10 s: accensione → LED → l'iPhone sul manubrio apre Maps senza essere toccato.
3. 10-15 s: primo piano della scheda: "$3 ESP32 · flash from browser · link in bio".

## 4. Metriche (controllale a 7 e 30 giorni)

- Stelle GitHub, visite al sito (GitHub → Insights → Traffic), download delle Release.
- **Report hardware nelle issue:** vale più di 100 stelle, perché ti dice su quali schede funziona davvero.
- Se dopo r/esp32 + r/shortcuts hai meno di 50 stelle, il problema non sono i canali
  ma il messaggio o il video: rifai il video prima di spammare altri subreddit.

## 5. Dopo il lancio

- Tabella "testato da utenti" nel README, aggiornata dalle issue.
- Template di issue "Hardware report" (scheda, chip, versione iOS, funziona sì/no).
- Esempi di Shortcuts condivisibili (link iCloud) per i 3 casi d'uso principali: abbassano la barriera più di qualsiasi riga di documentazione.
