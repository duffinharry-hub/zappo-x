#!/usr/bin/env python3
"""Clove boot screen in the style of an old PC "boot options" text screen:
black background, grey bars, monospace text. 640x480, for TF1 boot_logo."""
import sys
from PIL import Image, ImageDraw, ImageFont

MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
W, H = 640, 480


def render(accent=None):
    grey = (192, 192, 192)
    white = (255, 255, 255)
    bar = accent or grey
    bar_text = (0, 0, 0)
    im = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(MONO, 14)
    cw = f.getlength("M")
    L = 20                 # left margin
    ind = L + cw * 3       # menu indent
    row = 19.0

    def y(r):
        return 10 + r * row

    def text(x, r, s, col=grey):
        d.text((x, y(r)), s, font=f, fill=col)

    def full_bar(top, left="", centre="", right=""):
        d.rectangle([L, top, W - L, top + 19], fill=bar)
        ty = top + 2
        if left: d.text((L + 8, ty), left, font=f, fill=bar_text)
        if centre: d.text(((W - f.getlength(centre)) / 2, ty), centre, font=f, fill=bar_text)
        if right: d.text((W - L - 8 - f.getlength(right), ty), right, font=f, fill=bar_text)

    full_bar(6, centre="Clove Boot Options")
    text(L, 2, "Choose Advanced Options for: ")
    text(L + f.getlength("Choose Advanced Options for: "), 2, "Clove 0.3.3", white)
    text(L, 3, "(Use the D-pad to highlight your choice.)")

    # highlighted first choice
    s = "Start Clove"
    d.rectangle([L, y(5) - 2, ind + f.getlength(s) + 1, y(5) + 17], fill=bar)
    d.text((ind, y(5)), s, font=f, fill=bar_text)

    items = [
        (7, "Music Library (98 songs, 57 artists)"),
        (8, "Cover Art (92 albums, 57 artist photos)"),
        (9, "Videos (Big Stan)"),
        (11, "BLOCKBOX"),
        (12, "Import Folder Sorting"),
        (13, "Themes: Swirl Green"),
        (14, "Screensaver and Clock"),
        (15, "Button Sensitivity"),
        (16, "BIOS Files (384 installed)"),
        (17, "Boot from TF2 Card (1 TB)"),
        (19, "Start GarlicOS 1.4.9 Normally"),
    ]
    for r, s in items:
        text(ind, r, s)

    lab = "Description: "
    text(L, 20.8, lab)
    text(L + f.getlength(lab), 20.8, "Start Clove with your games, music, videos", grey)
    text(L + f.getlength(lab), 21.8, "and BLOCKBOX on the Anbernic RG35XX.", grey)
    full_bar(H - 26, left="A=Choose", centre="created by CovGNG", right="B=Cancel")
    return im


if __name__ == "__main__":
    out = sys.argv[1]
    render().save(out + "/clove-boot-options.png")
    render(accent=(195, 162, 247)).save(out + "/clove-boot-options-lavender.png")
    print("ok")
