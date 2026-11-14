#!/usr/bin/env python3
"""make_quickstart.py - build docs/Scratch-QuickStart.pdf (system python with Pillow; no other dependency):
    python3 -I tools/make_quickstart.py
Pages are drawn as images (US Letter, 150 dpi) and saved as a PDF. Figures: the Battle-page mockup, the HELP tab, and three
diagrams drawn here (folder layout, pad layout, split touch strip)."""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
DOCS = os.path.join(PROJ, "docs")
FIG = os.path.join(DOCS, "qs")
os.makedirs(FIG, exist_ok=True)
FB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FR = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FM = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
def font(path, size): return ImageFont.truetype(path, size)

INK, GOLD, DARK, GREY, RED = (28, 26, 24), (176, 116, 8), (30, 30, 32), (120, 120, 116), (170, 36, 24)
PW, PH, MARGIN = 1275, 1650, 105          # US Letter at 150 dpi


# ---- figures ----------------------------------------------------------------------------------------------------
def fig_install():
    """Folder tree: the existing VST2 Plugins folder with the new Scratch folder added next to the other plugins."""
    W, H = 1000, 430
    im = Image.new("RGB", (W, H), (245, 243, 238)); d = ImageDraw.Draw(im)
    f, fb = font(FM, 24), font(FB, 22)
    d.text((24, 16), "Your SD card / USB drive (top level)", font=fb, fill=INK)
    rows = [(0, "VST2 Plugins/", "your existing folder - keep it", GREY, False),
            (1, "Some Other Plugin/", "already there: leave it alone", GREY, False),
            (1, "Another Plugin/", "already there: leave it alone", GREY, False),
            (1, "Keebz - VST - Scratch/", "NEW - the only thing you add", (0, 120, 50), True),
            (2, "Content/Samples/", "your .wav samples go here", GREY, False),
            (2, "Plugin Skins/, VST/ ...", "leave these as they are", GREY, False)]
    y = 66
    for lvl, name, note, col, hot in rows:
        x = 40 + lvl * 44
        if hot: d.rounded_rectangle((x - 10, y - 6, W - 20, y + 38), 8, fill=(214, 240, 222), outline=(0, 120, 50), width=3)
        d.text((x, y), name, font=f, fill=INK if not hot else (0, 80, 30))
        d.text((560, y + 3), note, font=font(FR, 20), fill=col)
        y += 54
    im.save(os.path.join(FIG, "fig_install.png")); return im


def fig_pads():
    """The 16 MPC pads in chromatic mode, with the Scratch pad functions."""
    W, H = 760, 760
    im = Image.new("RGB", (W, H), (245, 243, 238)); d = ImageDraw.Draw(im)
    d.text((24, 14), "Pad layout (Chromatic, Bank B, Shot Base 36)", font=font(FB, 24), fill=INK)
    S, G, X0, Y0 = 150, 14, 60, 70
    names = {}
    for i in range(8): names[i + 1] = ("A%02d" % (i + 1), "sample %d" % (i + 1), (230, 160, 40))
    names[12] = ("HAMSTER", "note 47", (90, 150, 220))
    for pad in range(1, 17):
        r, c = (pad - 1) // 4, (pad - 1) % 4
        x, y = X0 + c * (S + G), Y0 + (3 - r) * (S + G)
        nm = names.get(pad)
        col = nm[2] if nm else (205, 203, 198)
        d.rounded_rectangle((x, y, x + S, y + S), 14, fill=col, outline=INK, width=3)
        d.text((x + 10, y + 8), "pad %d" % pad, font=font(FR, 18), fill=INK)
        d.text((x + 10, y + 8 + 22), "note %d" % (35 + pad), font=font(FR, 16), fill=(70, 70, 66))
        if nm:
            d.text((x + S // 2, y + 84), nm[0], font=font(FB, 24 if len(nm[0]) > 3 else 28), fill=INK, anchor="mm")
            d.text((x + S // 2, y + 118), nm[1], font=font(FR, 18), fill=INK, anchor="mm")
        else:
            d.text((x + S // 2, y + 100), "unused", font=font(FR, 18), fill=(120, 120, 116), anchor="mm")
    im.save(os.path.join(FIG, "fig_pads.png")); return im


def fig_strip():
    """The touch strip split in two, with what each part does."""
    W, H = 1000, 560
    im = Image.new("RGB", (W, H), (245, 243, 238)); d = ImageDraw.Draw(im)
    x0, x1, y0, y1 = 120, 220, 30, 530
    ym = (y0 + y1) // 2
    d.rectangle((x0, y0, x1, ym), fill=(205, 203, 198), outline=INK, width=3)
    d.rectangle((x0, ym, x1, y1), fill=(120, 200, 150), outline=INK, width=3)
    d.line((x0 - 30, ym, x1 + 30, ym), fill=RED, width=5)
    f, fb = font(FR, 22), font(FB, 24)
    d.text((270, 110), "TOP HALF  (ignored)", font=fb, fill=INK)
    d.text((270, 146), "Touching here just holds the record still.", font=f, fill=INK)
    d.text((270, ym - 30), "THE MIDDLE  =  \"finger lifted\"", font=fb, fill=RED)
    d.text((270, ym + 6), "The strip springs back to the middle when you let go,", font=f, fill=INK)
    d.text((270, ym + 36), "so the plugin reads the middle as a release.", font=f, fill=INK)
    d.text((270, 400), "BOTTOM HALF  (scratches)", font=fb, fill=(0, 100, 50))
    d.text((270, 436), "Slide here to move the record. (Default zone.)", font=f, fill=INK)
    d.text((270, 466), "Strip Zone on SETUP also offers Top, or Whole.", font=f, fill=GREY)
    im.save(os.path.join(FIG, "fig_strip.png")); return im


# ---- page layout ------------------------------------------------------------------------------------------------
class Doc:
    def __init__(self):
        self.pages, self.im, self.d, self.y = [], None, None, 0
        self.new_page()

    def new_page(self):
        if self.im: self.finish_page()
        self.im = Image.new("RGB", (PW, PH), (255, 255, 255)); self.d = ImageDraw.Draw(self.im)
        self.d.rectangle((0, 0, PW, 14), fill=(233, 168, 37))
        self.y = MARGIN

    def finish_page(self):
        n = len(self.pages) + 1
        self.d.text((MARGIN, PH - 70), "Keebz Scratch - Quick Start     (experimental software, provided AS IS)", font=font(FR, 18), fill=GREY)
        self.d.text((PW - MARGIN, PH - 70), "page %d" % n, font=font(FR, 18), fill=GREY, anchor="ra")
        self.pages.append(self.im)

    def room(self, h):
        if self.y + h > PH - 110: self.new_page()

    def wrap(self, text, f, width):
        lines = []
        for para in text.split("\n"):
            cur = ""
            for w in para.split(" "):
                t = (cur + " " + w).strip()
                if self.d.textlength(t, font=f) <= width: cur = t
                else: lines.append(cur); cur = w
            lines.append(cur)
        return lines

    def h1(self, text):
        f = font(FB, 46); self.room(140); self.y += 24
        self.d.text((MARGIN, self.y), text, font=f, fill=INK); self.y += 70

    def h2(self, text):
        f = font(FB, 30); self.room(110)
        self.y += 16; self.d.text((MARGIN, self.y), text, font=f, fill=GOLD); self.y += 48

    def p(self, text, size=23, color=INK, indent=0, gap=12, bold=False):
        f = font(FB if bold else FR, size)
        for line in self.wrap(text, f, PW - 2 * MARGIN - indent):
            self.room(size + 14)
            self.d.text((MARGIN + indent, self.y), line, font=f, fill=color); self.y += int(size * 1.5)
        self.y += gap

    def step(self, n, text):
        f = font(FR, 23)
        lines = self.wrap(text, f, PW - 2 * MARGIN - 70)
        self.room(len(lines) * 35 + 16)
        self.d.ellipse((MARGIN, self.y - 2, MARGIN + 40, self.y + 38), fill=(233, 168, 37))
        self.d.text((MARGIN + 20, self.y + 18), str(n), font=font(FB, 24), fill=INK, anchor="mm")
        for line in lines:
            self.d.text((MARGIN + 66, self.y), line, font=f, fill=INK); self.y += 35
        self.y += 12

    def bullet(self, text, indent=0):
        f = font(FR, 23)
        lines = self.wrap(text, f, PW - 2 * MARGIN - 40 - indent)
        self.room(len(lines) * 35 + 8)
        self.d.ellipse((MARGIN + indent + 6, self.y + 12, MARGIN + indent + 16, self.y + 22), fill=GOLD)
        for line in lines:
            self.d.text((MARGIN + indent + 36, self.y), line, font=f, fill=INK); self.y += 35
        self.y += 6

    def box(self, title, text, fill=(255, 244, 214), edge=(200, 140, 20), tcol=INK):
        f, fb = font(FR, 21), font(FB, 23)
        lines = self.wrap(text, f, PW - 2 * MARGIN - 50)
        h = 30 + 36 + len(lines) * 31 + 16
        self.room(h + 16)
        self.d.rounded_rectangle((MARGIN, self.y, PW - MARGIN, self.y + h), 12, fill=fill, outline=edge, width=3)
        self.d.text((MARGIN + 25, self.y + 18), title, font=fb, fill=tcol)
        yy = self.y + 18 + 38
        for line in lines:
            self.d.text((MARGIN + 25, yy), line, font=f, fill=INK); yy += 31
        self.y += h + 20

    def image(self, im, width=None, caption=None):
        width = width or (PW - 2 * MARGIN)
        im = im.convert("RGB"); h = int(im.height * width / im.width)
        self.room(h + 60)
        im = im.resize((width, h), Image.LANCZOS)
        x = (PW - width) // 2
        self.im.paste(im, (x, self.y)); self.d.rectangle((x, self.y, x + width, self.y + h), outline=(190, 188, 182), width=2)
        self.y += h + 8
        if caption:
            self.d.text((PW // 2, self.y), caption, font=font(FR, 18), fill=GREY, anchor="ma"); self.y += 34
        self.y += 10


def main():
    battle = Image.open(os.path.join(DOCS, "mockup.png"))
    helpimg = Image.open(os.path.join(PROJ, "skins", "bg_help.png"))
    helpimg.convert("RGB").save(os.path.join(DOCS, "help-tab.png"))
    install, pads, strip = fig_install(), fig_pads(), fig_strip()
    D = Doc()

    D.d.text((MARGIN, D.y), "KEEBZ SCRATCH", font=font(FB, 64), fill=INK); D.y += 92
    D.p("Quick Start", size=34, color=GOLD, bold=True, gap=14)
    D.p("A DJ scratch instrument plugin for the Akai MPC Live III. The touch strip is the record: touch it to grab the record, "
        "slide to scratch, lift your finger and it plays on. Pads A01-A08 hold samples and double as the crossfader, there is a "
        "two-channel mixer with DJ EQ, and an on-screen turntable that follows your hands.", gap=18)
    D.image(battle, caption="The BATTLE page (picture is a design mockup; fonts on the MPC differ slightly)")
    D.box("EXPERIMENTAL - PROVIDED \"AS IS\"",
          "Keebz Scratch is experimental software. It is provided \"AS IS\", without warranty of any kind, express or implied, "
          "including (but not limited to) warranties of merchantability, fitness for a particular purpose and non-infringement. "
          "You use it entirely at your own risk. The author is not liable for any damage, data loss or other claim arising from "
          "its use. Back up your projects and your card before installing. External plugin support on the MPC is itself a beta "
          "feature. Not affiliated with or endorsed by Akai Professional or inMusic; \"MPC\" and \"Akai\" belong to their owners.",
          fill=(255, 232, 228), edge=RED, tcol=RED)

    D.new_page()
    D.h1("1.  Install")
    D.p("You need: an MPC Live III running MPC OS 3.10.1 or later (check in Preferences > Info), an SD card or USB drive "
        "(or the MPC's internal drive in computer mode), and a computer.", gap=10)
    D.h2("A.  Your drive has no \"VST2 Plugins\" folder yet")
    D.step(1, "Unzip the downloaded file (Keebz - Scratch v1_0.zip) on your computer. Inside is one folder named VST2 Plugins, and inside that one folder named \"Keebz - VST - Scratch\".")
    D.step(2, "Copy the whole VST2 Plugins folder to the very top level of your SD card or USB drive (not inside another folder).")
    D.step(3, "Eject the drive safely, put it in the MPC, and go to \"Switch it on in the MPC\" below.")
    D.h2("B.  You already have a \"VST2 Plugins\" folder")
    D.box("DO NOT REPLACE YOUR EXISTING VST2 PLUGINS FOLDER",
          "Your other plugins live in it. If you drag the new VST2 Plugins folder onto the card and your computer asks to "
          "Replace the folder, choose Stop / Cancel. On a Mac, Replace deletes what was in your folder. Add only the Scratch folder, "
          "as below.", fill=(255, 232, 228), edge=RED, tcol=RED)
    D.step(1, "Unzip the download and open the VST2 Plugins folder inside it. You will see one folder: \"Keebz - VST - Scratch\".")
    D.step(2, "Open your drive and open its existing VST2 Plugins folder.")
    D.step(3, "Drag or copy only the \"Keebz - VST - Scratch\" folder into that existing VST2 Plugins folder. Keep the folder name exactly as it is.")
    D.image(install, width=900, caption="Result: Scratch sits next to your other plugins. Nothing else is touched.")
    D.p("Updating later: copy your own samples out of Content/Samples first, delete the old \"Keebz - VST - Scratch\" folder, copy the new one in, "
        "then put your samples back. Projects keep working because the plugin keeps its identity.", size=21, color=GREY)

    D.h2("Switch it on in the MPC")
    D.step(1, "On the MPC open Preferences > Enhancements (Beta) and turn on Enable Enhancements and External Plugin Support. Read the warning. The plugin stays hidden until both are on.")
    D.step(2, "Make a new track or program that uses a plugin and pick Scratch (by Keebz) from the plugin list. Menu wording can differ a little between MPC OS versions.")
    D.step(3, "If it does not appear: check the folder is called exactly \"Keebz - VST - Scratch\", sits inside a folder called exactly \"VST2 Plugins\" at the top of the drive, and that the OS is 3.10.1 or newer.")

    D.h1("2.  Set up the MPC (important)")
    D.p("Three things must be set on the MPC or the pads and the strip will not do what this guide says:", gap=8)
    D.step(1, "NOTES = CHROMATIC. Set the pad layout to Chromatic so the 16 pads send consecutive notes. Pad 1 must send note 36 (that is the plugin's Shot Base setting on the SETUP tab, default 36). Then pads 1-8 are the sample pads A01-A08 and pad 12 (note 47) is the hamster switch.")
    D.step(2, "TOUCH STRIP = PITCH BEND. Put the touch strip in Pitch Bend mode. The plugin scratches with the strip's pitch-bend messages. On the SETUP tab Strip Src should read Bend.")
    D.step(3, "PAD BANK = B. The pad layout is built for Bank B, so make sure Bank B is the active bank.")
    D.image(pads, width=640, caption="A01-A04 are the bottom row of pads, A05-A08 the row above. Pad 12 flips the hamster (crossfader reverse).")
    D.p("If a pad does nothing: check Chromatic, Bank B and the note the pad sends. If the strip does nothing or sounds wrong: check "
        "the strip mode is Pitch Bend and Strip Src is Bend.", size=21, color=GREY)

    D.h1("3.  Playing it")
    D.h2("The touch strip is the record")
    D.p("Touch the strip to grab the record, slide to scratch it forward and back, lift your finger and it plays on at its own speed. "
        "Throw on the SETUP tab is how far the record moves for one pull. Smooth and Lead trade softness against delay (the defaults 300 / 300 are a good start).")
    D.h2("Why the touch strip is split in half")
    D.p("The MPC's touch strip is really a pitch wheel. A pitch wheel springs back to the middle, so when you lift your finger the MPC sends "
        "\"middle\" - exactly the same message as touching the exact middle. The plugin cannot tell \"finger resting in the middle\" from "
        "\"finger lifted\", so a record scratched across the middle would hang or jump.", gap=8)
    D.p("The fix: use only half of the strip. Only the bottom half scratches (default). The middle of the strip then always means \"lifted\", "
        "and touching the top half just holds the record where it is. Throw is scaled so a half strip feels as long as a whole one. "
        "You can choose Top or Whole in Strip Zone on the SETUP tab; with Whole, the exact middle acts as a release.")
    D.image(strip, width=760, caption="Strip Zone (SETUP): Bottom (default), Top or Whole")
    D.h2("Pads, list and mixer")
    D.bullet("Pads A01-A08: each holds a sample. Hit one to put its sample on the record (at its sticker if it has one). While held, the pad opens the crossfader. Hit the same pad again and it only works the cut.")
    D.bullet("Sample list (turn the encoder): PREVIEW plays the selected sample on channel B. LOAD then a pad stores the selected sample on that pad. TO B loops it on channel B. STOP B stops channel B.")
    D.bullet("Tap the record on screen to stick a cue marker at the needle (one sticker). 33 / 45 / STOP set the platter speed.")
    D.bullet("Mixer: A is the deck, B is the loop. GAIN, HI, MID, LOW sit at 12 o'clock = flat. Gain goes to silent, EQ to kill.")
    D.bullet("Recording in the sequencer: strip and pad moves are recorded. Loop Snap (SETUP, on by default) puts the record back to its position at the start of the take every time the loop restarts, so your scratches land on the same part of the sample.")
    D.image(helpimg, width=1000, caption="The HELP tab inside the plugin repeats the essentials")

    D.h1("4.  Adding your own samples")
    D.step(1, "On your computer, open the drive, then VST2 Plugins > \"Keebz - VST - Scratch\" > Content > Samples.")
    D.step(2, "Copy your .wav files into that Samples folder. Delete the ones you do not want.")
    D.step(3, "Put the drive back in the MPC and load the plugin again (reload the project or the plugin). The new files appear in the sample list.")
    D.bullet("Files: WAV (mono or stereo, 8 to 32-bit, any sample rate). Up to 32 files are used. Anything longer than 6 seconds is cut at 6 seconds.")
    D.bullet("Order: files are listed by name. Start names with numbers to set the order (01 Kick.wav, 02 Snare.wav). The leading number is not shown; names show in capitals, up to 9 characters.")
    D.bullet("Pads point at list positions: pad A01 = 1st file, A02 = 2nd, and so on, until you change them with LOAD. If you add or remove files, check which sample is on which pad.")
    D.bullet("Beat_Loop.wav is a loop meant for channel B: select it and press TO B. You can replace it with your own loop.")
    D.h2("Troubleshooting")
    D.bullet("Plugin not listed: check the two Enhancements switches, the folder names and the OS version (section 1).")
    D.bullet("Pads silent or play the wrong sample: Notes = Chromatic, Bank B, Shot Base 36 (section 2).")
    D.bullet("Strip does nothing or the record zips on release: strip mode Pitch Bend, Strip Src Bend, Strip Zone Bottom.")
    D.bullet("Report a bug: use the DEBUG build, repeat the problem, and send the file scratch_log.txt it writes in the plugin folder.")
    D.h2("By Keebz")
    D.p("Instagram: @leekebler", gap=2); D.p("GitHub: github.com/keebz", gap=2); D.p("Web: Keebz.com", gap=14)
    D.box("EXPERIMENTAL - PROVIDED \"AS IS\"",
          "This software is experimental and comes with no warranties of any kind, express or implied. It is provided AS IS and you use it at your own risk. "
          "The author is not liable for any damage or loss arising from its use.", fill=(255, 232, 228), edge=RED, tcol=RED)

    D.finish_page()
    out = os.path.join(DOCS, "Scratch-QuickStart.pdf")
    D.pages[0].save(out, "PDF", resolution=150.0, save_all=True, append_images=D.pages[1:])
    for i, pg in enumerate(D.pages): pg.save(os.path.join(FIG, "page%d.png" % (i + 1)))
    print("wrote", out, "(%d pages)" % len(D.pages))


main()
