import asyncio
import subprocess
from AppKit import NSPasteboard, NSStringPboardType
from bleak import BleakScanner, BleakClient

NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

queue = asyncio.Queue()
pb = NSPasteboard.generalPasteboard()


def on_notify(_, data):
    queue.put_nowait(data.decode(errors="ignore").strip())


def get_frontmost_app():
    script = ('tell application "System Events" to get name of first '
              'application process whose frontmost is true')
    r = subprocess.run(["osascript", "-e", script],
                       capture_output=True, text=True)
    return r.stdout.strip()


def get_browser_url(app_name):
    """Recupere l'URL de l'onglet actif selon le navigateur."""
    if app_name == "Safari":
        script = 'tell application "Safari" to get URL of current tab of front window'
    elif app_name in ("Google Chrome", "Brave Browser"):
        script = f'tell application "{app_name}" to get URL of active tab of front window'
    else:
        return None

    r = subprocess.run(["osascript", "-e", script],
                       capture_output=True, text=True)
    if r.returncode == 0 and r.stdout.strip():
        return r.stdout.strip()
    return None


# Noms exacts des apps tels que macOS les identifie
BROWSERS = {"Safari", "Google Chrome", "Brave Browser"}


async def capture_selection():
    """Tente de recuperer le texte selectionne via Cmd+C.
    Retourne le texte, ou None si rien n'a ete copie."""
    saved = pb.stringForType_(NSStringPboardType)
    before = pb.changeCount()

    subprocess.run(["osascript", "-e",
        'tell application "System Events" to keystroke "c" using command down'])

    captured = None
    for _ in range(20):
        await asyncio.sleep(0.05)
        if pb.changeCount() != before:
            captured = pb.stringForType_(NSStringPboardType)
            break

    if saved is not None:
        pb.clearContents()
        pb.setString_forType_(saved, NSStringPboardType)

    return captured.strip() if captured else None


async def handle_trigger(client):
    # 1. Priorite au texte selectionne
    captured = await capture_selection()
    source = "selection"

    # 2. Sinon, URL du navigateur actif
    if not captured:
        app = get_frontmost_app()
        if app in BROWSERS:
            url = get_browser_url(app)
            if url:
                captured = url
                source = f"url:{app}"

    # 3. Rien a capturer
    if not captured:
        app = get_frontmost_app()
        print(f"Rien a capturer (app active : {app})")
        await client.write_gatt_char(NUS_RX, b"ERRLINK", response=False)
        return

    # On a quelque chose : signal de traitement
    await client.write_gatt_char(NUS_RX, b"BUSY", response=False)

    print("-" * 60)
    print(f"[{source}]")
    print(captured)
    print("-" * 60)

    # === PIPELINE A VENIR ===
    # detection URL vs texte, scraping, LLM, vectorisation
    try:
        await asyncio.sleep(1.5)  # simulation du traitement
        await client.write_gatt_char(NUS_RX, b"OK", response=False)
    except Exception as e:
        print(f"Erreur traitement : {e}")
        await client.write_gatt_char(NUS_RX, b"ERRAI", response=False)


async def session():
    print("Recherche de CoolButton...")
    device = None
    for d in await BleakScanner.discover(timeout=8.0):
        if d.name == "CoolButton":
            device = d
            break

    if device is None:
        print("  -> pas trouve, nouvel essai dans 3s")
        return

    print(f"  -> trouve : {device.address}")
    async with BleakClient(device) as client:
        print("Connecte.")
        await client.start_notify(NUS_TX, on_notify)
        while client.is_connected:
            try:
                msg = await asyncio.wait_for(queue.get(), timeout=1.0)
            except asyncio.TimeoutError:
                continue
            if msg == "TRIG":
                await handle_trigger(client)


async def main():
    print("Daemon CoolButton demarre.")
    while True:
        try:
            await session()
        except Exception as e:
            print(f"Erreur: {e}")
        await asyncio.sleep(3)


asyncio.run(main())