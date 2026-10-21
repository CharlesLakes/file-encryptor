"""Records docs/demo.gif by driving the FileEncryptor GUI with synthetic X11 events.

@author  @CharlesLakes

Maintainer: @CharlesLakes

The GUI is started in <working dir>, which must contain the sample files
foto-playa.jpg, notas.txt and presupuesto.xlsx and an empty folder extraido/.
The script creates vacaciones.crls there, adds the three files, closes it,
opens it with a wrong and then with the right password, and extracts
everything into extraido/. Needs an X11 session, wmctrl, xwd and Pillow.

usage: python3 docs/record_demo.py build/file-encryptor <working dir> docs/demo.gif
"""
import ctypes, os, struct, subprocess, sys, time
from PIL import Image, ImageDraw

BINARY, WORKDIR, OUTPUT = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3]

x = ctypes.CDLL("libX11.so.6")
x.XOpenDisplay.restype = ctypes.c_void_p
x.XStringToKeysym.restype = ctypes.c_ulong
x.XKeysymToKeycode.restype = ctypes.c_ubyte
x.XKeycodeToKeysym.restype = ctypes.c_ulong


class Ev(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int),
                ("display", ctypes.c_void_p), ("window", ctypes.c_ulong), ("root", ctypes.c_ulong),
                ("subwindow", ctypes.c_ulong), ("time", ctypes.c_ulong), ("x", ctypes.c_int), ("y", ctypes.c_int),
                ("x_root", ctypes.c_int), ("y_root", ctypes.c_int), ("state", ctypes.c_uint),
                ("code", ctypes.c_uint), ("same_screen", ctypes.c_int), ("pad", ctypes.c_long * 16)]


dpy = x.XOpenDisplay(None)
proc = subprocess.Popen([BINARY], cwd=WORKDIR)
win = None
for _ in range(50):
    time.sleep(0.1)
    out = subprocess.run(["wmctrl", "-l"], capture_output=True, text=True).stdout
    rows = [l.split()[0] for l in out.splitlines() if l.endswith(".crls manager")]
    if rows:
        win = int(rows[-1], 16)
        break
assert win, "window not found"
time.sleep(0.8)

frames = []                     # (image, milliseconds)
cursor = [380, 300]             # where the fake mouse pointer is drawn
NAMES = {":": "colon", "/": "slash", ".": "period", "-": "minus", "\t": "Tab", "\n": "Return", " ": "space"}


def grab():
    data = subprocess.run(["xwd", "-id", hex(win), "-silent"], capture_output=True).stdout
    h = struct.unpack(">25I", data[:100])
    header_size, w, ht, order, bpl, ncolors = h[0], h[4], h[5], h[7], h[12], h[19]
    offset = header_size + ncolors * 12
    img = Image.frombytes("RGB", (w, ht), data[offset:offset + bpl * ht], "raw",
                          "BGRX" if order == 0 else "XRGB", bpl)
    draw = ImageDraw.Draw(img)
    cx, cy = cursor
    arrow = [(cx, cy), (cx, cy + 17), (cx + 4, cy + 13), (cx + 7, cy + 20), (cx + 10, cy + 19),
             (cx + 7, cy + 12), (cx + 12, cy + 12)]
    draw.polygon(arrow, fill=(255, 255, 255), outline=(0, 0, 0))
    return img


def frame(ms):
    frames.append((grab(), ms))


def send(ev, mask):
    x.XSendEvent(ctypes.c_void_p(dpy), ctypes.c_ulong(win), 1, ctypes.c_long(mask), ctypes.byref(ev))
    x.XFlush(ctypes.c_void_p(dpy))


def key(name, shift_for=None):
    sym = x.XStringToKeysym(name.encode())
    kc = x.XKeysymToKeycode(ctypes.c_void_p(dpy), ctypes.c_ulong(sym))
    shift = 0 if x.XKeycodeToKeysym(ctypes.c_void_p(dpy), kc, 0) == sym else 1
    send(Ev(type=2, send_event=1, display=dpy, window=win, state=shift, code=kc, same_screen=1), 1)
    time.sleep(0.03)


def type_text(text, ms=55):
    for i, ch in enumerate(text):
        key(NAMES.get(ch, ch))
        if i % 2 == 1 or i == len(text) - 1:
            time.sleep(0.02)
            frame(ms)


def press(name, ms=400, settle=0.15):
    key(name)
    time.sleep(settle)
    frame(ms)


def click(px, py, ms=500, settle=0.2, steps=4):
    # move the fake pointer there in a few frames, then click
    sx, sy = cursor
    for i in range(1, steps + 1):
        cursor[0] = sx + (px - sx) * i // steps
        cursor[1] = sy + (py - sy) * i // steps
        frame(40)
    send(Ev(type=4, send_event=1, display=dpy, window=win, x=px, y=py, code=1, same_screen=1), 1 << 2)
    time.sleep(settle)
    frame(ms)


def wait_until_idle(seconds, ms=150):
    end = time.time() + seconds
    while time.time() < end:
        frame(ms)
        time.sleep(0.05)


BUTTON_Y = 487
frame(1200)                                       # the open screen

# 1. Create a new archive.
type_text("vacaciones")
press("Tab", 200)
type_text("playa-2026")
press("Tab", 200)
type_text("playa-2026")
click(160, BUTTON_Y, 200, settle=0.05)            # Create new
wait_until_idle(2.2)
frame(1200)

# 2. Add three files through the folder browser.
for downs in (2, 3, 4):        # ../ and extraido/ come first
    click(47, BUTTON_Y, 700)                      # Add
    for _ in range(downs):
        press("Down", 250)
    press("Return", 200, settle=0.4)
    frame(700)
frame(1300)

# 3. Close it and open it again: first with a wrong password.
click(592, BUTTON_Y, 900)                         # Close
click(90, 127, 500)                               # vacaciones.crls in the list
click(400, 401, 300)                              # password field
type_text("otra-clave")
press("Return", 200, settle=0.05)
wait_until_idle(2.0)
frame(1500)
for _ in range(10):
    key("BackSpace")
frame(200)
type_text("playa-2026")
press("Return", 200, settle=0.05)
wait_until_idle(2.0)
frame(1300)

# 4. Extract everything into a folder.
click(265, BUTTON_Y, 700)                         # Extract all
press("Down", 300)                                # extraido/
press("Return", 600, settle=0.3)
click(88, BUTTON_Y, 300, settle=0.6)              # Extract here
frame(2500)

proc.terminate()
proc.wait()

# Merge identical consecutive frames, then write the GIF with one palette.
merged = []
for img, ms in frames:
    if merged and list(merged[-1][0].getdata()) == list(img.getdata()):
        merged[-1] = (merged[-1][0], merged[-1][1] + ms)
    else:
        merged.append((img, ms))
palette = merged[len(merged) // 2][0].quantize(colors=64, method=Image.Quantize.MEDIANCUT)
images = [img.quantize(palette=palette, dither=Image.Dither.NONE) for img, _ in merged]
# Long identical stretches (waiting for Argon2id) are shortened to keep the GIF brief.
images[0].save(OUTPUT, save_all=True, append_images=images[1:], duration=[min(ms, 1600) for _, ms in merged],
               loop=0, optimize=True, disposal=1)
print(f"{len(frames)} frames captured, {len(merged)} written to {OUTPUT}")
