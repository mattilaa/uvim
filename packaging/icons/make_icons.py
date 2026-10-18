"""Generates the uVim icons from code (no external artwork).

    python3 packaging/icons/make_icons.py

Writes packaging/icons/uvim-1024.png, packaging/windows/uvim.ico and, on
macOS (iconutil), packaging/macos/uVim.icns plus the installer background.
Needs Pillow and the Menlo font (macOS).
"""
import os
import shutil
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT = "/System/Library/Fonts/Menlo.ttc"


def icon(size):
    scale = size / 256.0
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((8 * scale, 8 * scale, size - 8 * scale, size - 8 * scale),
                        radius=44 * scale, fill=(31, 29, 46, 255),
                        outline=(110, 106, 134, 255), width=max(1, round(6 * scale)))
    font = ImageFont.truetype(FONT, round(118 * scale), index=1)
    text = "uV"
    bb = d.textbbox((0, 0), text, font=font)
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    d.text(((size - w) / 2 - bb[0], (size - h) / 2 - bb[1] - 14 * scale), text,
           font=font, fill=(224, 222, 244, 255))
    d.rectangle((60 * scale, 196 * scale, 196 * scale, 212 * scale),
                fill=(62, 143, 176, 255))
    return img


def main():
    master = icon(1024)
    master.save(os.path.join(ROOT, "packaging/icons/uvim-1024.png"))
    icon(256).save(os.path.join(ROOT, "packaging/windows/uvim.ico"),
                   sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
                          (128, 128), (256, 256)])

    # Installer window background: the icon in the bottom-left corner.
    background = Image.new("RGBA", (620, 418), (0, 0, 0, 0))
    background.paste(icon(128), (24, 418 - 128 - 24))
    background.save(os.path.join(ROOT, "packaging/macos/installer-background.png"))

    if shutil.which("iconutil"):
        with tempfile.TemporaryDirectory() as tmp:
            iconset = os.path.join(tmp, "uVim.iconset")
            os.mkdir(iconset)
            for base in (16, 32, 128, 256, 512):
                icon(base).save(os.path.join(iconset, f"icon_{base}x{base}.png"))
                icon(base * 2).save(os.path.join(iconset, f"icon_{base}x{base}@2x.png"))
            subprocess.run(["iconutil", "-c", "icns", iconset, "-o",
                            os.path.join(ROOT, "packaging/macos/uVim.icns")], check=True)


if __name__ == "__main__":
    main()
