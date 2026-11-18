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


def german_keys():
    text = read("src/core/i18n_de.cpp")
    return {unescape(m.group(1)) for m in re.finditer(r'\{"((?:[^"\\]|\\.)*)",\s*"(?:[^"\\]|\\.)*"\}', text)}


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
    for path in ("src/core/preset.cpp", "src/audio/audio_input_win.cpp", "src/audio/audio_input_other.cpp", "src/update/updater.cpp", "src/update/update_logic.cpp", "src/platform/platform_win.cpp", "src/platform/platform_other.cpp", "src/control/control_system.cpp", "src/control/udp_socket_posix.cpp", "src/control/udp_socket_win.cpp", "src/control/midi_input_win.cpp", "src/control/midi_input_other.cpp"):
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
    print("All %d translation keys are covered" % len(have))
    return 0


if __name__ == "__main__":
    sys.exit(main())
