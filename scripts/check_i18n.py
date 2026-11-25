import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IGNORED = {"HTTP "}


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as handle:
        return handle.read()


def unescape(text):
    return text.replace('\\"', '"').replace("\\\\", "\\")


ENTRY = re.compile(r'\{"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\}')
LANGUAGE_FILES = {"de": "src/core/i18n_de.cpp", "es": "src/core/i18n_es.cpp", "fr": "src/core/i18n_fr.cpp", "it": "src/core/i18n_it.cpp", "pt": "src/core/i18n_pt.cpp", "nl": "src/core/i18n_nl.cpp", "ja": "src/core/i18n_ja.cpp"}
SPECIFIER = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z]")


def table(path):
    text = read(path)
    return [(unescape(m.group(1)), unescape(m.group(2))) for m in ENTRY.finditer(text)]


def german_keys():
    return {key for key, _ in table(LANGUAGE_FILES["de"])}


def check_languages():
    problems = []
    reference = table(LANGUAGE_FILES["de"])
    reference_keys = [key for key, _ in reference]
    for code, path in LANGUAGE_FILES.items():
        entries = table(path)
        keys = [key for key, _ in entries]
        if len(set(keys)) != len(keys):
            problems.append("%s: duplicate keys" % code)
        if set(keys) != set(reference_keys):
            for key in sorted(set(reference_keys) - set(keys)):
                problems.append("%s: missing key %r" % (code, key))
            for key in sorted(set(keys) - set(reference_keys)):
                problems.append("%s: extra key %r" % (code, key))
        for key, value in entries:
            if not value.strip():
                problems.append("%s: empty text for %r" % (code, key))
            if key.endswith(" ") != value.endswith(" ") or key.startswith(" ") != value.startswith(" "):
                problems.append("%s: edge spaces differ for %r" % (code, key))
            if code not in ("de", "ja") and any(ord(ch) > 255 for ch in value):
                problems.append("%s: character outside Latin-1 in %r" % (code, key))
            if sorted(SPECIFIER.findall(key)) != sorted(SPECIFIER.findall(value)) and key != "Slowest 5%":
                problems.append("%s: format specifiers differ for %r" % (code, key))
    return problems


def literal_keys():
    keys = set()
    pattern = re.compile(r'(?<![A-Za-z_])tr(?:Id)?\(\s*"((?:[^"\\]|\\.)*)"')
    for path in glob.glob(os.path.join(ROOT, "src", "**", "*.cpp"), recursive=True) + glob.glob(os.path.join(ROOT, "src", "**", "*.hpp"), recursive=True):
        if path.endswith("i18n_de.cpp"):
            continue
        with open(path, encoding="utf-8") as handle:
            for match in pattern.finditer(handle.read()):
                keys.add(unescape(match.group(1)))
    return keys


def table_keys():
    text = read("src/core/composition.cpp")
    keys = set()
    for name in ("kBlendNames", "kBandNames", "kTargetNames", "kTransitionNames", "kReactModeNames"):
        match = re.search(name + r"\[[A-Za-z]+\]\s*=\s*\{([^}]*)\}", text)
        if match:
            keys.update(re.findall(r'"([^"]+)"', match.group(1)))
    return keys


def parameter_keys():
    keys = set()
    for path in ("src/render/effect_library.cpp", "src/render/effects_extra.cpp"):
        for match in re.finditer(r'fx\.push_back\(\{"[^"]+", \{([^}]*)\}', read(path)):
            keys.update(re.findall(r'"([^"]+)"', match.group(1)))
    return keys


def message_keys():
    keys = set()
    pattern = re.compile(r'(?:error|message|fail|set\(UpdateState::[A-Za-z]+,)\s*(?:=|\()?\s*"((?:[^"\\]|\\.)*)"')
    for path in ("src/core/preset.cpp", "src/audio/audio_input_win.cpp", "src/audio/audio_input_other.cpp", "src/update/updater.cpp", "src/update/update_logic.cpp", "src/platform/platform_win.cpp", "src/platform/platform_other.cpp", "src/platform/platform_linux_update.cpp", "src/media/ffmpeg_setup.cpp", "src/media/hap_convert.cpp", "src/media/video_recorder.cpp", "src/core/record_plan.cpp", "src/media/convert_manager.cpp", "src/core/archive.cpp", "src/core/mask_images.cpp", "src/render/output_window.cpp", "src/control/control_system.cpp", "src/control/udp_socket_posix.cpp", "src/control/udp_socket_win.cpp", "src/control/midi_input_win.cpp", "src/control/midi_input_other.cpp"):
        for match in pattern.finditer(read(path)):
            key = unescape(match.group(1))
            if re.match(r"^[A-Z][A-Za-z ,.'()-]+[A-Za-z.: )]$", key) and key not in IGNORED:
                keys.add(key)
    return keys


def main():
    have = german_keys()
    missing = []
    for source in (literal_keys(), table_keys(), parameter_keys(), message_keys()):
        for key in sorted(source):
            if key not in have:
                missing.append(key)
    if missing:
        print("Missing German translations:")
        for key in missing:
            print("  " + key)
        return 1
    problems = check_languages()
    if problems:
        print("Language table problems:")
        for problem in problems:
            print("  " + problem)
        return 1
    print("All %d translation keys are covered" % len(have))
    return 0


if __name__ == "__main__":
    sys.exit(main())
