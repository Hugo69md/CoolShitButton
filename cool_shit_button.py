import subprocess
from datetime import datetime

text = subprocess.run(["pbpaste"], capture_output=True, text=True).stdout.strip()

if not text:
    subprocess.run(["osascript", "-e",
        'display notification "Rien de selectionne" with title "CoolButton"'])
    raise SystemExit

with open("/tmp/coolbutton.log", "a") as f:
    f.write(f"\n=== {datetime.now():%H:%M:%S} ===\n{text}\n")

subprocess.run(["osascript", "-e",
    f'display notification "{len(text)} caracteres captures" with title "CoolButton"'])