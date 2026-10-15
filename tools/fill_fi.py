#!/usr/bin/env python3
"""Fills translations/harbour-edgesync-fi.ts from the English extraction base.

lupdate maintains the message list; this holds the Finnish text. Run it after
lupdate (see README) and it reports any source string that has no translation,
so a new qsTr() cannot slip through untranslated unnoticed.
"""
import html
import re
import shutil
import sys

# "harjoitus" (workout) is the word for an activity throughout, as in the
# Finnish of Suunto, Polar and Garmin's own apps.
FI = {
"About Edge Sync": "Tietoja: Edge Sync",
"Edge Sync downloads the activities recorded by a Garmin Edge bike computer over Bluetooth LE and shows them. It is an independent project: it is not made, endorsed or supported by Garmin.":
    "Edge Sync lataa Garmin Edge -pyörätietokoneen tallentamat harjoitukset Bluetooth LE:n yli ja näyttää ne. Tämä on riippumaton projekti: Garmin ei ole tehnyt, hyväksynyt eikä tue sitä.",
"Privacy": "Yksityisyys",
"Credits": "Tekijät ja kiitokset",
"Garmin's Bluetooth protocol was reverse engineered by the Gadgetbridge project (AGPL-3.0). garmin-ble, garmin-bridge and GarminBLE showed how to speak it from Linux. This app is free software under the same licence.":
    "Garminin Bluetooth-protokollan on selvittänyt Gadgetbridge-projekti (AGPL-3.0). garmin-ble, garmin-bridge ja GarminBLE näyttivät, miten sitä puhutaan Linuxista. Tämä sovellus on vapaa ohjelmisto samalla lisenssillä.",
"Data folder": "Datakansio",
"Cycling": "Pyöräily",
"Indoor cycling": "Sisäpyöräily",
"Mountain biking": "Maastopyöräily",
"Gravel cycling": "Gravel-pyöräily",
"Cyclocross": "Cyclocross",
"Track cycling": "Ratapyöräily",
"Hand cycling": "Käsipyöräily",
"E-biking": "Sähköpyöräily",
"Running": "Juoksu",
"Treadmill": "Juoksumatto",
"Trail running": "Polkujuoksu",
"Walking": "Kävely",
"Hiking": "Vaellus",
"Swimming": "Uinti",
"Training": "Harjoittelu",
"Fitness equipment": "Kuntolaite",
"Rowing": "Soutu",
"Cross-country skiing": "Hiihto",
"Alpine skiing": "Laskettelu",
"Activity": "Harjoitus",
"Delete": "Poista",
"Deleting": "Poistetaan",
"Save the FIT file": "Tallenna FIT-tiedosto",
"Save as GPX": "Tallenna GPX-tiedostona",
"Rename": "Nimeä uudelleen",
"Saved to %1": "Tallennettu: %1",
"This file could not be read: %1. It is kept on the phone, and a later version of the app may read it.":
    "Tätä tiedostoa ei voitu lukea: %1. Se säilytetään puhelimessa, ja sovelluksen myöhempi versio voi osata lukea sen.",
"Distance": "Matka",
"Moving time": "Liikkumisaika",
"Average speed": "Keskinopeus",
"Top speed": "Huippunopeus",
"Climbed": "Nousu",
"Average heart rate": "Keskisyke",
"Max heart rate": "Maksimisyke",
"Average power": "Keskiteho",
"Normalized power": "Normalisoitu teho",
"Average cadence": "Keskitrampitus",
"Energy": "Energia",
"Elapsed time": "Kulunut aika",
"Route": "Reitti",
"Speed (km/h)": "Nopeus (km/h)",
"Elevation (m)": "Korkeus (m)",
"Heart rate (bpm)": "Syke (bpm)",
"Power (W)": "Teho (W)",
"Cadence (rpm)": "Trampitus (rpm)",
"Temperature (°C)": "Lämpötila (°C)",
"Laps": "Kierrokset",
"File": "Tiedosto",
"records": "tietuetta",
"The file's checksum does not match: it may be damaged.": "Tiedoston tarkistussumma ei täsmää: se voi olla vioittunut.",
"The file ends early.": "Tiedosto päättyy ennenaikaisesti.",
"Could not open the local database: %1": "Paikallista tietokantaa ei voitu avata: %1",
"Could not write %1": "Kirjoitus epäonnistui: %1",
"Bluetooth does not know this device any more. Scan again.": "Bluetooth ei enää tunne tätä laitetta. Hae uudelleen.",
"Pairing…": "Pariutetaan…",
"Paired with %1": "Pariutettu: %1",
"Another operation is still running.": "Toinen toiminto on vielä käynnissä.",
"Choose an Edge first.": "Valitse ensin Edge.",
"Bluetooth does not know this Edge any more. Choose it again and pair it.":
    "Bluetooth ei enää tunne tätä Edgeä. Valitse se uudelleen ja pariuta se.",
"Connecting to the Edge…": "Yhdistetään Edgeen…",
"Cancelled.": "Peruttu.",
"Talking to the Edge…": "Keskustellaan Edgen kanssa…",
"Could not save an activity: %1": "Harjoitusta ei voitu tallentaa: %1",
"Reading the Edge's file list…": "Luetaan Edgen tiedostolistaa…",
"Downloading activity %1 of %2…": "Ladataan harjoitusta %1/%2…",
"The Bluetooth link to the Edge was lost.": "Bluetooth-yhteys Edgeen katkesi.",
"Nothing new": "Ei uutta",
"The sync failed.": "Synkronointi epäonnistui.",
"The Edge lists no activity files over Bluetooth LE. Some older models (the Edge 520 Plus, for one) send their files only over classic Bluetooth, which this app does not support yet. The log has the Edge's file list.":
    "Edge ei listaa yhtään harjoitustiedostoa Bluetooth LE:n yli. Jotkin vanhemmat mallit (esimerkiksi Edge 520 Plus) lähettävät tiedostonsa vain perinteisellä Bluetoothilla, jota tämä sovellus ei vielä tue. Lokissa on Edgen tiedostolista.",
"Could not delete the activity.": "Harjoitusta ei voitu poistaa.",
"The file is missing.": "Tiedosto puuttuu.",
"Could not create %1": "Kansiota ei voitu luoda: %1",
"The activity file could not be read.": "Harjoituksen tiedostoa ei voitu lukea.",
"This activity has no GPS track to export.": "Tässä harjoituksessa ei ole vietävää GPS-jälkeä.",
"No rides yet": "Ei vielä harjoituksia",
"No Edge set up": "Edgeä ei ole otettu käyttöön",
"Forget this Edge": "Unohda tämä Edge",
"Scan again": "Hae uudelleen",
"Your Edge": "Edgesi",
"On the Edge, open Settings > Connectivity > Phone and choose Pair Smartphone, then pick it below. If it is already paired with your phone in Bluetooth settings, you can pick it straight away.":
    "Avaa Edgessä Asetukset > Yhteydet > Puhelin ja valitse Pariuta älypuhelin, ja valitse se sitten alta. Jos se on jo pariutettu puhelimeen Bluetooth-asetuksissa, voit valita sen heti.",
"paired": "pariutettu",
"not paired": "ei pariutettu",
"connected": "yhdistetty",
"selected": "valittu",
"Looking for Edges…": "Etsitään Edgejä…",
"No Edge found": "Edgeä ei löytynyt",
"Switch the Edge on and keep it next to the phone": "Kytke Edge päälle ja pidä se puhelimen vieressä",
"Clear the log": "Tyhjennä loki",
"Save the log as a file": "Tallenna loki tiedostoon",
"Log": "Loki",
"Nothing has happened yet": "Mitään ei ole vielä tapahtunut",
"Could not reach the Edge. Is it on, nearby, and not connected to another phone?":
    "Edgeen ei saatu yhteyttä. Onko se päällä, lähellä eikä yhdistettynä toiseen puhelimeen?",
"The Edge connected but did not finish Bluetooth service discovery.": "Edge yhdistyi, mutta Bluetooth-palveluiden haku ei valmistunut.",
"The Edge did not open its sync channel.": "Edge ei avannut synkronointikanavaansa.",
"The Edge refused to pair (%1). Open the pairing screen on the Edge (Settings > Connectivity > Phone > Pair Smartphone) and try again.":
    "Edge kieltäytyi pariutumasta (%1). Avaa Edgen pariutusnäyttö (Asetukset > Yhteydet > Puhelin > Pariuta älypuhelin) ja yritä uudelleen.",
"Pairing timed out. Put the Edge in pairing mode and keep it next to the phone.":
    "Pariutus aikakatkaistiin. Aseta Edge pariutustilaan ja pidä se puhelimen vieressä.",
"Pairing failed: %1": "Pariutus epäonnistui: %1",
"Could not connect to the Edge: %1": "Edgeen ei voitu yhdistää: %1",
"The Edge dropped the connection.": "Edge katkaisi yhteyden.",
"Could not list Bluetooth services: %1": "Bluetooth-palveluita ei voitu listata: %1",
"This device does not offer Garmin's sync service. Is it a Garmin Edge, and is it paired?":
    "Tämä laite ei tarjoa Garminin synkronointipalvelua. Onko se Garmin Edge, ja onko se pariutettu?",
"The Edge refused notifications because the link is not encrypted. Pair the Edge first.":
    "Edge kieltäytyi ilmoituksista, koska yhteys ei ole salattu. Pariuta Edge ensin.",
"Could not subscribe to the Edge: %1": "Edgen ilmoituksia ei voitu tilata: %1",
"Could not write to the Edge: %1": "Edgeen ei voitu kirjoittaa: %1",
"Could not reach Bluetooth (org.bluez): %1": "Bluetoothiin (org.bluez) ei saatu yhteyttä: %1",
"No Bluetooth adapter found": "Bluetooth-sovitinta ei löytynyt",
"Could not list Bluetooth devices: %1": "Bluetooth-laitteita ei voitu listata: %1",
"Could not start scanning: %1": "Hakua ei voitu aloittaa: %1",
"No data": "Ei dataa",
"synced just now": "synkronoitu juuri nyt",
"synced %1 ago": "synkronoitu %1 sitten",
"About": "Tietoja",
"Settings": "Asetukset",
"Change Edge": "Vaihda Edge",
"Set up your Edge": "Ota Edge käyttöön",
"Sync now": "Synkronoi nyt",
"Edge Sync": "Edge Sync",
"Cancel": "Peruuta",
"Your rides, on your phone": "Harjoituksesi puhelimessasi",
"unreadable file": "tiedostoa ei voi lukea",
"Pull down and choose Sync now": "Vedä alas ja valitse Synkronoi nyt",
"Name": "Nimi",
"Leave empty to show the kind of activity instead.": "Jätä tyhjäksi, niin näytetään harjoituksen laji.",
"Sync": "Synkronointi",
"Mark rides as synced on the Edge": "Merkitse harjoitukset synkronoiduiksi Edgessä",
"Sets the Edge's archive flag on every ride once it is saved here. Leave this off if you also use Garmin Connect or another sync: they decide what is new by the same flag.":
    "Asettaa Edgen arkistointilipun jokaiselle harjoitukselle, kun se on tallennettu tänne. Pidä pois päältä, jos käytät myös Garmin Connectia tai muuta synkronointia: ne päättävät, mikä on uutta, samalla lipulla.",
"Ask the Edge for new files only": "Pyydä Edgeltä vain uudet tiedostot",
"Asks the Edge to list only files not marked as synced. Faster, but it hides rides that another app has already marked.":
    "Pyytää Edgeä listaamaan vain tiedostot, joita ei ole merkitty synkronoiduiksi. Nopeampi, mutta piilottaa harjoitukset, jotka jokin toinen sovellus on jo merkinnyt.",
"Set the Edge's clock": "Aseta Edgen kello",
"Tells the Edge the phone's time zone and time. An Edge with GPS keeps its own time, so this is normally not needed.":
    "Kertoo Edgelle puhelimen aikavyöhykkeen ja ajan. GPS:llä varustettu Edge pitää oman aikansa, joten tätä ei yleensä tarvita.",
"Disconnect after syncing": "Katkaise yhteys synkronoinnin jälkeen",
"Lets go of the Edge when done, so Garmin Connect or another phone can use it.":
    "Päästää Edgestä irti valmistuttua, jotta Garmin Connect tai toinen puhelin voi käyttää sitä.",
"Help": "Ohje",
"Experiments": "Kokeilut",
"Write without waiting for confirmation": "Kirjoita odottamatta vahvistusta",
"Sends each answer to the Edge without waiting for the Edge to confirm it, which may make downloads faster. Takes effect on the next sync.": "Lähettää jokaisen vastauksen Edgelle odottamatta Edgen vahvistusta, mikä voi nopeuttaa latauksia. Tulee voimaan seuraavassa synkronoinnissa.",
"Tell the Edge only what a sync needs": "Kerro Edgelle vain synkronoinnin tarvitsema",
"Announces a short list of abilities, so the Edge asks for fewer things this app cannot do (weather, notifications) while a file downloads. If syncing stops working, turn this off.": "Ilmoittaa lyhyen listan kyvyistä, jolloin Edge pyytää tiedoston latauksen aikana vähemmän asioita, joita tämä sovellus ei osaa (sää, ilmoitukset). Jos synkronointi lakkaa toimimasta, kytke pois.",
"Rides are kept as the original FIT files in the app's own folder on this phone and are never sent anywhere; exports go to Documents/EdgeSync. The app's only use of the network is the optional base map under the route, which is off by default: when it is on, the tile server you choose in Settings sees which area you look at, and nothing else.":
    "Harjoitukset säilytetään alkuperäisinä FIT-tiedostoina sovelluksen omassa kansiossa tässä puhelimessa, eikä niitä lähetetä minnekään; viennit menevät kansioon Documents/EdgeSync. Sovellus käyttää verkkoa vain valinnaiseen pohjakarttaan reitin alla, joka on oletuksena pois päältä: kun se on päällä, Asetuksissa valitsemasi karttapalvelin näkee, minkä alueen katsot, eikä mitään muuta.",
"Edge Sync downloads the activities from a Garmin Edge bike computer over Bluetooth. There is no account and no cloud: the files stay on this phone, and you can export them as GPX or FIT, or import a FIT file from another device.":
    "Edge Sync lataa harjoitukset Garmin Edge -pyörätietokoneelta Bluetoothilla. Ei tiliä eikä pilveä: tiedostot pysyvät tässä puhelimessa, ja voit viedä ne GPX- tai FIT-muodossa tai tuoda FIT-tiedoston toiselta laitteelta.",
"Could not open %1: %2": "Tiedostoa %1 ei voitu avata: %2",
"%1 is not a readable FIT activity: %2": "%1 ei ole luettava FIT-harjoitus: %2",
"%1 has no start time, so it cannot be filed.": "Tiedostolla %1 ei ole alkuaikaa, joten sitä ei voi tallentaa.",
"%1 is already here (a ride with the same start time).": "%1 on jo täällä (harjoitus samalla alkuajalla).",
"Could not save %1: %2": "Tiedostoa %1 ei voitu tallentaa: %2",
"Imported %1": "Tuotu: %1",
"Choose a FIT file": "Valitse FIT-tiedosto",
"Import a FIT file": "Tuo FIT-tiedosto",
"That address serves image tiles, which this version cannot draw yet. A vector source such as the default works.": "Kyseinen osoite tarjoaa kuvalaattoja, joita tämä versio ei vielä osaa piirtää. Vektorilähde, kuten oletus, toimii.",
"No map tile address set": "Karttalaattojen osoitetta ei ole asetettu",
"That address did not answer with TileJSON. Give a {z}/{x}/{y} template instead.": "Kyseinen osoite ei vastannut TileJSONilla. Anna sen sijaan {z}/{x}/{y}-malli.",
"The map source listed no tiles": "Karttalähde ei luetellut yhtään laattaa",
"Map": "Kartta",
"Show a base map under the route": "Näytä pohjakartta reitin alla",
"Off by default: the map tiles come from somebody else's servers, which learn the area of every ride you open. The ride itself is never sent.": "Oletuksena pois: karttalaatat tulevat jonkun toisen palvelimilta, jotka näkevät jokaisen avaamasi harjoituksen alueen. Itse harjoitusta ei lähetetä koskaan.",
"Tile address": "Laattojen osoite",
"Either a TileJSON address or a {z}/{x}/{y} template. The default is OpenFreeMap, which needs no account and allows this use; its attribution is drawn on the map. Vector tiles only for now - an address serving images will say so.": "Joko TileJSON-osoite tai {z}/{x}/{y}-malli. Oletus on OpenFreeMap, joka ei vaadi tiliä ja sallii tämän käytön; sen lähdemerkintä piirretään kartalle. Toistaiseksi vain vektorilaatat — kuvia tarjoava osoite kertoo siitä.",
"Back to the default source": "Takaisin oletuslähteeseen",
"Record Bluetooth traffic in the log": "Tallenna Bluetooth-liikenne lokiin",
"Adds every byte sent to and received from the Edge to the log. Turn on to report a problem; the log can then be saved as a text file.":
    "Lisää lokiin jokaisen Edgelle lähetetyn ja siltä vastaanotetun tavun. Kytke päälle ongelmaa ilmoittaessa; loki voidaan sitten tallentaa tekstitiedostoksi.",
"Open the log": "Avaa loki",
}

# Plural messages: source -> [singular, plural]. Finnish has two forms.
FI_PLURAL = {
"%n new activities": ["%n uusi harjoitus", "%n uutta harjoitusta"],
}


def main():
    base, out = "translations/harbour-edgesync.ts", "translations/harbour-edgesync-fi.ts"
    shutil.copyfile(base, out)
    text = open(out, encoding="utf-8").read()
    text = text.replace('<TS version="2.1">', '<TS version="2.1" language="fi_FI">', 1) \
        if 'language=' not in text.split('\n')[1] else text
    missing = []

    def fill(match):
        block = match.group(0)
        src = html.unescape(re.search(r"<source>(.*?)</source>", block, re.S).group(1))
        if 'numerus="yes"' in block.split('>', 1)[0] + '>':
            if src not in FI_PLURAL:
                missing.append(src)
                return block
            forms = "".join("<numerusform>%s</numerusform>" % html.escape(f, quote=False) for f in FI_PLURAL[src])
            return re.sub(r"<translation[^>]*>.*?</translation>|<translation[^>]*/>",
                          "<translation>%s</translation>" % forms, block, flags=re.S)
        if src not in FI:
            missing.append(src)
            return block
        esc = html.escape(FI[src], quote=False)
        return re.sub(r"<translation[^>]*>.*?</translation>|<translation[^>]*/>",
                      "<translation>%s</translation>" % esc, block, flags=re.S)

    text = re.sub(r"<message(?: numerus=\"yes\")?>.*?</message>", fill, text, flags=re.S)
    open(out, "w", encoding="utf-8").write(text)
    sources = {html.unescape(m) for m in re.findall(r"<source>(.*?)</source>", text, re.S)}
    for m in missing:
        print("UNTRANSLATED:", repr(m))
    for k in list(FI) + list(FI_PLURAL):
        if k not in sources:
            print("unused entry:", repr(k[:60]))
    return 1 if missing else 0


sys.exit(main())
