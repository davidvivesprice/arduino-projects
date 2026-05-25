#!/usr/bin/env python3
"""
Generate per-room bag inserts for the TPS×Vives Sonos volume-control boards.

For each of the 7 rooms it produces:
  - <slug>.png  — high-res QR code in TPS red on white, links to tpsvc-<slug>.local
  - <slug>.html — single printable card (cut to ~4×6") with logo, room, QR, instructions

It also produces:
  - all_cards.html — one sheet with all 7 cards laid out, ready to print + cut
"""
from pathlib import Path
import base64
import qrcode
from qrcode.image.styledpil import StyledPilImage
from qrcode.image.styles.moduledrawers.pil import RoundedModuleDrawer
from qrcode.image.styles.colormasks import SolidFillColorMask

HERE = Path(__file__).resolve().parent
# Local transparent-background copy of the TPS logo (black BG path stripped).
LOGO_SVG = HERE / "tps_logo_transparent.svg"
TPS_RED = "#8B0000"

ROOMS = [
    ("mstbed",  "Master Bedroom"),
    ("mstbth",  "Master Bathroom"),
    ("grkit",   "Great Room<br>Kitchen"),
    ("grliv",   "Great Room<br>Living"),
    ("fam",     "Family Room"),
    ("bed2",    "Bedroom 2"),
    ("bed3",    "Bedroom 3"),
]

# ── QR generation ─────────────────────────────────────────────────────────
def make_qr(url: str, out_path: Path):
    qr = qrcode.QRCode(
        version=None,
        error_correction=qrcode.constants.ERROR_CORRECT_H,  # high error correction
        box_size=18,                                         # pixels per module
        border=2,
    )
    qr.add_data(url)
    qr.make(fit=True)
    img = qr.make_image(
        image_factory=StyledPilImage,
        module_drawer=RoundedModuleDrawer(),
        color_mask=SolidFillColorMask(
            back_color=(255, 255, 255),
            front_color=(0, 0, 0),  # black — logo is the only red accent
        ),
    )
    img.save(out_path)

# ── Logo as data URI so cards are self-contained ──────────────────────────
def logo_data_uri() -> str:
    svg = LOGO_SVG.read_text()
    b64 = base64.b64encode(svg.encode()).decode()
    return f"data:image/svg+xml;base64,{b64}"

# ── HTML card template ────────────────────────────────────────────────────
CARD_CSS = """
@page { size: A4; margin: 10mm; }
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: -apple-system, "Helvetica Neue", Helvetica, Arial, sans-serif;
  background: white; color: #111;
}
/* A4 printable area (with 10mm @page margins): 190 × 277 mm.
   2 cols × 2 rows of cards per page, 5mm gutter:
     card width  = (190 - 5) / 2 = 92.5mm
     card height = (277 - 5) / 2 = 136mm
   Comfortable for a 2.5cm "safety zone" inside that on top of border. */
.sheet {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 5mm;
  padding: 0;
}
.card {
  width: 92mm; height: 134mm;
  border: 1.5pt solid #1a1a1a;
  border-radius: 6pt;
  padding: 7mm 7mm 5.5mm;
  display: flex; flex-direction: column;
  page-break-inside: avoid;
  background: white;
  overflow: hidden;
}
.card .logo {
  display: flex; justify-content: center; align-items: center;
  height: 15mm;
  flex-shrink: 0;
}
.card .logo img { max-height: 100%; max-width: 65%; }
.card .room {
  font-family: Georgia, "Times New Roman", serif;
  font-size: 22pt; font-weight: 400;
  color: #000;
  text-align: center;
  margin-top: 4pt;
  letter-spacing: -0.5px;
  line-height: 1.1;
  flex-shrink: 0;
}
.card .host {
  font-family: ui-monospace, "SF Mono", "Menlo", monospace;
  font-size: 13pt; font-weight: 600;
  color: #8B0000;
  text-align: center;
  margin-top: 4pt;
  letter-spacing: 0.5px;
  flex-shrink: 0;
}
.card .qr {
  flex: 1;
  display: flex; justify-content: center; align-items: center;
  min-height: 0;
}
.card .qr img { width: 46mm; height: 46mm; }
.card .steps {
  font-size: 8.5pt; line-height: 1.4;
  color: #000;
  border-top: 0.5pt solid #ccc;
  padding-top: 6pt;
  flex-shrink: 0;
}
.card .steps ol { margin-left: 13pt; }
.card .steps li { margin-bottom: 1pt; }
.card .footer {
  text-align: center;
  font-family: ui-monospace, "SF Mono", monospace;
  font-size: 6.5pt; color: #555;
  margin-top: 5pt; letter-spacing: 1px; text-transform: uppercase;
  flex-shrink: 0;
}
"""

CARD_TEMPLATE = """<div class="card">
  <div class="logo"><img src="{logo}" alt="TPS Audio"></div>
  <div class="room">{room}</div>
  <div class="host">tpsvc-{slug}.local</div>
  <div class="qr"><img src="{slug}.png" alt="QR to controller"></div>
  <div class="steps">
    <ol>
      <li>Plug the knob ribbon into the side of the controller.</li>
      <li>Plug the Ethernet cable into PoE. Wait 10 seconds.</li>
      <li>Scan the QR code (or visit <strong>tpsvc-{slug}.local</strong>).</li>
    </ol>
  </div>
  <div class="footer">TPS × Vives · Sonos Ethernet Edition</div>
</div>"""

def main():
    logo = logo_data_uri()
    cards_html = []
    for slug, label in ROOMS:
        url = f"http://tpsvc-{slug}.local/"
        png_path = HERE / f"{slug}.png"
        make_qr(url, png_path)
        print(f"✓ {png_path.name}  →  {url}")

        # Individual card HTML
        single = f"""<!doctype html><html><head><meta charset="utf-8">
<title>{label} — TPS Audio</title><style>{CARD_CSS}
.sheet {{ display: flex; justify-content: center; align-items: center; min-height: 100vh; }}
</style></head><body><div class="sheet">{CARD_TEMPLATE.format(logo=logo, room=label, slug=slug)}</div></body></html>"""
        (HERE / f"{slug}.html").write_text(single)

        cards_html.append(CARD_TEMPLATE.format(logo=logo, room=label, slug=slug))

    # Combined sheet — 2 columns × 4 rows = 8 slots, 7 rooms fill 7 of them
    all_html = f"""<!doctype html><html><head><meta charset="utf-8">
<title>TPS Audio · Sonos Volume Controllers · All Rooms</title>
<style>{CARD_CSS}</style></head><body>
<div class="sheet">{''.join(cards_html)}</div>
</body></html>"""
    (HERE / "all_cards.html").write_text(all_html)
    print(f"\n→ all_cards.html generated with {len(ROOMS)} cards")

    # Auto-render to A4 PDF using Chrome headless — browser display doesn't
    # honor @page CSS, only print/PDF does. This gives you the true A4 layout
    # to preview, share, and print.
    import shutil, subprocess
    for chrome in [
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
    ]:
        if Path(chrome).exists():
            pdf = HERE / "all_cards.pdf"
            subprocess.run(
                [chrome, "--headless", "--disable-gpu", "--no-sandbox",
                 f"--print-to-pdf={pdf}", "--no-pdf-header-footer",
                 f"file://{HERE / 'all_cards.html'}"],
                capture_output=True
            )
            print(f"→ all_cards.pdf  (A4, ready to print at 100% scale)")
            return
    print("   (Chrome not found — open all_cards.html in browser → Cmd+P to print)")

if __name__ == "__main__":
    main()
