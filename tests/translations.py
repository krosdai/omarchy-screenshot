# SPDX-License-Identifier: GPL-3.0-only

"""Check catalog completeness and the installed executable's locale selection."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


parser = argparse.ArgumentParser()
parser.add_argument("--app", required=True, type=Path)
parser.add_argument("--lupdate", required=True, type=Path)
parser.add_argument("--source", required=True, type=Path)
args = parser.parse_args()

LOCALES = set("""az be bg hr cs da nl et fi fr ka de el he hu is ga it ja kk ky lo
lv lt mk nb pl pt_PT pt_BR ro ru sr sk sl es sv tg tr uk zh_CN zh_TW""".split())
DESCRIPTION = ("main", "Capture and annotate screenshots on Hyprland.")
SESSION_ERROR = (
    "CaptureController", "Run this application in a Hyprland Wayland session."
)


def messages(path):
    result = {}
    for context in ET.parse(path).getroot().findall("context"):
        for message in context.findall("message"):
            key = (context.findtext("name"), message.findtext("source"))
            assert key not in result, (path, "duplicate message", key)
            result[key] = message.find("translation")
    return result


class Translations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalogs = {
            path.stem.removeprefix("omarchy-screenshot_"): messages(path)
            for path in (args.source / "translations").glob("*.ts")
        }
        cls.env = {
            key: value for key, value in os.environ.items()
            if key not in {"LANG", "LANGUAGE", "OMARCHY_SCREENSHOT_LANGUAGE"}
            and not key.startswith("LC_")
        }
        cls.env.update(QT_QPA_PLATFORM="offscreen", LANG="en_US.UTF-8",
                       XDG_SESSION_TYPE="", HYPRLAND_INSTANCE_SIGNATURE="",
                       QT_FORCE_STDERR_LOGGING="1")

    def run_app(self, options=(), env=None, code=0):
        result = subprocess.run(
            [str(args.app), *options], env=self.env | (env or {}),
            text=True, capture_output=True, timeout=10,
        )
        self.assertEqual(result.returncode, code, result.stderr)
        return " ".join((result.stdout + result.stderr).split())

    def description(self, locale):
        return " ".join(self.catalogs[locale][DESCRIPTION].text.split())

    def test_complete_catalogs(self):
        self.assertEqual(set(self.catalogs), LOCALES)
        with tempfile.TemporaryDirectory() as temp:
            reference = Path(temp) / "reference.ts"
            subprocess.run([
                str(args.lupdate), str(args.source / "src/main.cpp"),
                str(args.source / "src/capturecontroller.cpp"),
                str(args.source / "src/virtualpointer.cpp"),
                *(str(args.source / "qml" / name) for name in (
                    "Overlay.qml", "AnnotationToolbar.qml", "LongOverlay.qml",
                    "ScrollCapture.qml")),
                "-locations", "none",
                "-ts", str(reference),
            ], check=True, capture_output=True, text=True)
            sources = set(messages(reference))
        self.assertIn(DESCRIPTION, sources)
        self.assertIn(SESSION_ERROR, sources)
        for locale, catalog in self.catalogs.items():
            with self.subTest(locale=locale):
                self.assertEqual(set(catalog), sources)
                root = ET.parse(args.source / "translations" /
                                f"omarchy-screenshot_{locale}.ts").getroot()
                self.assertEqual(root.attrib["language"], locale)
                for (_, source), translation in catalog.items():
                    self.assertNotIn(translation.get("type"),
                                     {"unfinished", "vanished", "obsolete"})
                    self.assertTrue(translation.text and translation.text.strip())
                    self.assertEqual(re.findall(r"%\d+", source),
                                     re.findall(r"%\d+", translation.text))
                    for token in ("Qt", "wl-copy", "tesseract", "PNG", "OCR",
                                  "Alt", "Enter", "Shift+Enter", "Shift", "1 px"):
                        if token in source:
                            self.assertIn(token, translation.text)

    def test_hebrew_direction_isolates(self):
        catalog = self.catalogs["he"]
        selection = catalog[("AnnotationToolbar", "Selection · Arrow keys expand, Shift+arrows shrink (1 px)")].text
        # Without isolation the visible unit reverses to "px 1" in RTL text.
        self.assertEqual(selection.count("\u20661 px\u2069"), 1)
        self.assertIn("\u2066Shift\u2069", selection)
        editor = catalog[("AnnotationToolbar", "Enter: confirm · Shift+Enter: new line · Alt: confirm & return to previous tool")].text
        for key in ("Enter", "Shift+Enter", "Alt"):
            self.assertIn(f"\u2066{key}\u2069", editor)
        for translation in catalog.values():
            text = translation.text
            self.assertEqual(text.count("\u2066") + text.count("\u2068"),
                             text.count("\u2069"))
            if "%1" in text:
                self.assertIn("\u2068%1\u2069", text)

    def test_every_embedded_catalog(self):
        for locale in LOCALES:
            with self.subTest(locale=locale):
                self.assertIn(self.description(locale), self.run_app(
                    ["--language", locale, "--help"]))
                error = self.catalogs[locale][SESSION_ERROR].text
                self.assertIn(" ".join(error.split()), self.run_app(
                    ["--language", locale], code=1))

    def test_locale_precedence_and_aliases(self):
        for options, env, expected in [
            ([], {"LANG": "de_DE.UTF-8"}, "de"),
            ([], {"LANG": "en_US.UTF-8", "LC_MESSAGES": "uk_UA.UTF-8"}, "uk"),
            ([], {"LANG": "de_DE.UTF-8", "LC_ALL": "fr_FR.UTF-8"}, "fr"),
            ([], {"LANGUAGE": "fr:de"}, "fr"),
            ([], {"LANGUAGE": "ar:de"}, "de"),
            ([], {"LANGUAGE": "en:de"}, "en"),
            ([], {"LANG": "de_DE.UTF-8", "OMARCHY_SCREENSHOT_LANGUAGE": "ja"}, "ja"),
            (["--language=zh_TW"], {"OMARCHY_SCREENSHOT_LANGUAGE": "de"}, "zh_TW"),
            (["--language", "fr_CA"], {}, "fr"),
            (["--language", "de_CH"], {}, "de"),
            (["--language", "es_MX"], {}, "es"),
            (["--language", "pt-BR"], {}, "pt_BR"),
            (["--language", "pt"], {}, "pt_BR"),
            (["--language", "pt_PT"], {}, "pt_PT"),
            (["--language", "zh-Hans"], {}, "zh_CN"),
            (["--language", "zh-Hant"], {}, "zh_TW"),
            (["--language", "zh_HK"], {}, "zh_TW"),
            (["--language", "no_NO"], {}, "nb"),
            (["--language", "ar"], {}, "en"),
            (["--language", "invalid"], {}, "en"),
            (["--language", "en_GB"], {"LANGUAGE": "zh_CN"}, "en"),
            ([], {"LANG": "C"}, "en"),
        ]:
            with self.subTest(options=options, env=env):
                expected_text = DESCRIPTION[1] if expected == "en" else self.description(expected)
                self.assertIn(expected_text, self.run_app([*options, "--help"], env))


if __name__ == "__main__":
    unittest.main(argv=["translations.py"])
