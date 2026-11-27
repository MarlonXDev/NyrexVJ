#include "check.hpp"
#include "core/contrast.hpp"
#include "core/settings.hpp"
#include "core/shortcuts.hpp"
#include "core/workspace.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;
namespace ws = nvj::workspace;
namespace sc = nvj::shortcuts;

const char* kLayout =
    "[Window][deck]\n"
    "Pos=0,23\n"
    "Size=1600,466\n"
    "Collapsed=0\n"
    "DockId=0x00000001,0\n"
    "\n"
    "[Window][output]\n"
    "Pos=0,491\n"
    "Size=399,211\n"
    "Collapsed=0\n"
    "DockId=0x00000005,0\n"
    "\n"
    "[Docking][Data]\n"
    "DockSpace   ID=0x7C6B3D9B Window=0xA87D555D Pos=0,23 Size=1600,848 Split=Y\n"
    "  DockNode  ID=0x00000001 Parent=0x7C6B3D9B SizeRef=1600,466 Selected=0x1\n"
    "  DockNode  ID=0x00000002 Parent=0x7C6B3D9B SizeRef=1600,380 Split=X\n";

fs::path scratch(const std::string& name) {
    const fs::path path = fs::temp_directory_path() / ("nvj-interface-" + name);
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
    return path;
}

std::string readAll(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void writeAll(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

bool validUtf8(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t extra = 0;
        if (c < 0x80) {
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
        } else {
            return false;
        }
        for (size_t k = 1; k <= extra; ++k) {
            if (i + k >= text.size() || (static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += extra + 1;
    }
    return true;
}

void testNames() {
    CHECK(ws::cleanName("Show 4K") == "Show 4K");
    CHECK(ws::cleanName("  Show   4K  ") == "Show 4K");
    CHECK(ws::cleanName("a/b\\c:d") == "a b c d");
    CHECK(ws::cleanName("what?*|<>\"") == "what");
    CHECK(ws::cleanName("...") == "");
    CHECK(ws::cleanName("name.") == "name");
    CHECK(ws::cleanName("tab\tand\nline") == "tabandline");
    CHECK(ws::cleanName("") == "");
    CHECK(ws::cleanName("Gr\xC3\xB6\xC3\x9F" "e") == "Gr\xC3\xB6\xC3\x9F" "e");
    std::string longName;
    for (int i = 0; i < 30; ++i) {
        longName += "\xC3\xA4";
    }
    const std::string cut = ws::cleanName(longName);
    CHECK(cut.size() <= ws::kMaxNameBytes);
    CHECK(!cut.empty());
    CHECK(validUtf8(cut));
    CHECK(cut.size() % 2 == 0);
    CHECK(ws::cleanName(std::string(100, 'x')).size() == ws::kMaxNameBytes);
}

void testReservedAndKeys() {
    CHECK(ws::reservedName("CON"));
    CHECK(ws::reservedName("con"));
    CHECK(ws::reservedName("nul"));
    CHECK(ws::reservedName("COM1"));
    CHECK(ws::reservedName("lpt9"));
    CHECK(ws::reservedName("aux.txt"));
    CHECK(!ws::reservedName("COM10"));
    CHECK(!ws::reservedName("Console"));
    CHECK(!ws::reservedName("Show"));
    CHECK(ws::nameKey("Show") == ws::nameKey("sHOW"));
    CHECK(ws::nameKey("Show") != ws::nameKey("Shows"));
}

void testLayoutCheck() {
    const ws::Check good = ws::inspectLayout(kLayout);
    CHECK(good.ok);
    CHECK(good.windows == 2);
    CHECK(good.dockNodes == 3);
    CHECK(!ws::inspectLayout("").ok);
    std::string withNul = "[Window][a]\nPos=0,0\n";
    withNul += '\0';
    withNul += "Size=1,1\n";
    CHECK(!ws::inspectLayout(withNul).ok);
    CHECK(!ws::inspectLayout("[Window][a]\nPos=0,0\x01\n").ok);
    CHECK(!ws::inspectLayout("; only a comment\n").ok);
    CHECK(!ws::inspectLayout("[Docking][Data]\nDockSpace ID=0x1 Pos=0,0 Size=10,10\n").ok);
    CHECK(!ws::inspectLayout("Pos=0,0\n[Window][a]\n").ok);
    CHECK(!ws::inspectLayout("[Window]a\nPos=0,0\n").ok);
    CHECK(!ws::inspectLayout("[Window][]\nPos=0,0\n").ok);
    CHECK(!ws::inspectLayout("[Window][a]\nPos 0,0\n").ok);
    CHECK(!ws::inspectLayout("[Window][a]\nPos=99999,0\n").ok);
    CHECK(!ws::inspectLayout("[Window][a]\nPos=abc,0\n").ok);
    CHECK(!ws::inspectLayout("[Window][a]\nPos=1\n").ok);
    CHECK(!ws::inspectLayout("[Window][a]\nSize=-5,10\n").ok);
    CHECK(!ws::inspectLayout("[Window][a]\nSize=40000,10\n").ok);
    CHECK(!ws::inspectLayout("[Window][a]\nViewportPos=1000000,0\n").ok);
    CHECK(!ws::inspectLayout("[Window][a]\nSize=10,10\n[Docking][Data]\nGarbage here\n").ok);
    CHECK(!ws::inspectLayout("[Window][a]\nSize=10,10\n[Docking][Data]\nDockNode ID=0x1 Size=99999,5\n").ok);
    CHECK(ws::inspectLayout("[Window][a]\nSize=0,0\n").ok);
    CHECK(ws::inspectLayout("; comment\n\n[Window][a]\r\nPos=-20,5\r\nSize=10,10\r\n").ok);
    std::string big = "[Window][a]\n";
    while (big.size() <= ws::kMaxFileBytes) {
        big += "Pos=0,0\n";
    }
    CHECK(!ws::inspectLayout(big).ok);
    std::string many = "[Window][a]\n";
    for (int i = 0; i < ws::kMaxLines; ++i) {
        many += "A=1\n";
    }
    CHECK(!ws::inspectLayout(many).ok);
    std::string windows;
    for (int i = 0; i < 401; ++i) {
        windows += "[Window][w" + std::to_string(i) + "]\nPos=0,0\n";
    }
    CHECK(!ws::inspectLayout(windows).ok);
    CHECK(ws::inspectLayout("[Window][a]\nPos=0,0\n").reason.empty());
    CHECK(!ws::inspectLayout("garbage").reason.empty());
}

void testFileHeader() {
    const std::string file = ws::compose(kLayout, 1600, 900, "0.51.0");
    ws::Header header;
    const ws::Check check = ws::inspectFile(file, &header);
    CHECK(check.ok);
    CHECK(header.format == ws::kFormat);
    CHECK(header.width == 1600);
    CHECK(header.height == 900);
    CHECK(header.app == "0.51.0");
    CHECK(file.rfind("; NyrexVJ workspace\n", 0) == 0);
    CHECK(file.back() == '\n');
    CHECK(!ws::inspectFile(kLayout).ok);
    CHECK(!ws::inspectFile("").ok);
    std::string newer = file;
    newer.replace(newer.find("format=1"), 8, "format=9");
    const ws::Check refused = ws::inspectFile(newer);
    CHECK(!refused.ok);
    CHECK(refused.reason.find("newer version") != std::string::npos);
    std::string noFormat = file;
    noFormat.erase(noFormat.find("; format=1\n"), 11);
    CHECK(!ws::inspectFile(noFormat).ok);
    CHECK(!ws::inspectFile("; NyrexVJ workspace\n; format=1\n; display=10x10\n").ok);
    CHECK(ws::inspectFile(ws::compose("[Window][a]\nPos=0,0", 10, 20, "x")).ok);
}

void testStore() {
    const fs::path dir = scratch("store");
    CHECK(ws::list(dir).empty());
    CHECK(ws::save(dir, "Show 4K", kLayout, 3840, 2160, "0.51.0").ok);
    CHECK(ws::save(dir, "Club", kLayout, 1920, 1080, "0.51.0").ok);
    CHECK(ws::save(dir, "Gr\xC3\xB6\xC3\x9F" "e", kLayout, 1280, 720, "0.51.0").ok);
    std::vector<ws::Entry> entries = ws::list(dir);
    CHECK(entries.size() == 3);
    if (entries.size() == 3) {
        CHECK(entries[0].name == "Club");
        CHECK(entries[1].name == "Gr\xC3\xB6\xC3\x9F" "e");
        CHECK(entries[2].name == "Show 4K");
        CHECK(entries[2].width == 3840);
        CHECK(entries[2].height == 2160);
        CHECK(entries[2].valid);
    }
    std::string layout;
    ws::Header header;
    CHECK(ws::load(dir, "Show 4K", layout, &header).ok);
    CHECK(header.width == 3840);
    CHECK(ws::inspectLayout(layout).ok);
    CHECK(layout.find("[Window][deck]") != std::string::npos);
    CHECK(ws::load(dir, "show 4k", layout).ok);
    CHECK(!ws::load(dir, "missing", layout).ok);
    CHECK(ws::save(dir, "SHOW 4K", kLayout, 1000, 700, "0.51.0").ok);
    CHECK(ws::list(dir).size() == 3);
    CHECK(ws::load(dir, "Show 4K", layout, &header).ok);
    CHECK(header.width == 1000);
    CHECK(!ws::save(dir, "", kLayout, 1, 1, "x").ok);
    CHECK(!ws::save(dir, "///", kLayout, 1, 1, "x").ok);
    CHECK(!ws::save(dir, "CON", kLayout, 1, 1, "x").ok);
    CHECK(!ws::save(dir, "Bad", "nothing useful", 1, 1, "x").ok);
    CHECK(!ws::save(dir, "Bad", "", 1, 1, "x").ok);
    CHECK(ws::list(dir).size() == 3);
    CHECK(ws::remove(dir, "club").ok);
    CHECK(ws::list(dir).size() == 2);
    CHECK(!ws::remove(dir, "club").ok);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

void testShare() {
    const fs::path dir = scratch("share");
    const fs::path out = scratch("share-out");
    CHECK(ws::save(dir, "Projector night", kLayout, 1920, 1080, "0.62.0").ok);
    CHECK(ws::save(dir, "Gr\xC3\xB6\xC3\x9F" "e", kLayout, 1280, 720, "0.62.0").ok);

    fs::path written;
    CHECK(ws::exportTo(dir, "projector NIGHT", out, &written).ok);
    CHECK(written == out / "Projector night.nyxlayout");
    CHECK(fs::is_regular_file(written));
    CHECK(ws::inspectFile(readAll(written)).ok);
    CHECK(readAll(written) == readAll(ws::folder(dir) / "Projector night.ini"));
    CHECK(ws::exportTo(dir, "Gr\xC3\xB6\xC3\x9F" "e", out, &written).ok);
    CHECK(fs::is_regular_file(out / fs::path(std::u8string(u8"Gr\u00F6\u00DFe.nyxlayout"))));
    CHECK(ws::exportTo(dir, "Projector night", out).ok);

    const fs::path fallback = ws::exportFolder(dir);
    CHECK(ws::exportTo(dir, "Projector night", fs::path(), &written).ok);
    CHECK(written == fallback / "Projector night.nyxlayout");

    const ws::Result missing = ws::exportTo(dir, "nothing", out);
    CHECK(!missing.ok);
    CHECK(!missing.error.empty());
    writeAll(ws::folder(dir) / "Broken.ini", "[Window][broken\nPos=");
    CHECK(!ws::exportTo(dir, "Broken", out).ok);
    CHECK(!fs::exists(out / "Broken.nyxlayout"));
    writeAll(out / "blocker", "a file where a folder should be");
    CHECK(!ws::exportTo(dir, "Projector night", out / "blocker").ok);

    const fs::path other = scratch("share-other");
    std::string name;
    CHECK(ws::importFile(other, out / "Projector night.nyxlayout", &name).ok);
    CHECK(name == "Projector night");
    std::string layout;
    ws::Header header;
    CHECK(ws::load(other, "Projector night", layout, &header).ok);
    CHECK(header.width == 1920);
    CHECK(header.height == 1080);
    CHECK(header.app == "0.62.0");
    CHECK(layout == readAll(out / "Projector night.nyxlayout"));

    CHECK(ws::importFile(other, out / "Projector night.nyxlayout", &name).ok);
    CHECK(name == "Projector night 2");
    CHECK(ws::importFile(other, out / "Projector night.nyxlayout", &name).ok);
    CHECK(name == "Projector night 3");
    CHECK(ws::list(other).size() == 3);
    CHECK(ws::load(other, "projector night", layout).ok);
    CHECK(ws::importFile(other, out / fs::path(std::u8string(u8"Gr\u00F6\u00DFe.nyxlayout")), &name).ok);
    CHECK(name == "Gr\xC3\xB6\xC3\x9F" "e");

    const std::string longName(60, 'A');
    const fs::path longFile = out / (longName + ".nyxlayout");
    writeAll(longFile, readAll(out / "Projector night.nyxlayout"));
    CHECK(ws::importFile(other, longFile, &name).ok);
    CHECK(name.size() == ws::kMaxNameBytes);
    CHECK(ws::importFile(other, longFile, &name).ok);
    CHECK(name.size() <= ws::kMaxNameBytes);
    CHECK(name.size() >= 2 && name.substr(name.size() - 2) == " 2");
    CHECK(ws::list(other).size() == 6);

    const std::string umlauts = "\xC3\xA4\xC3\xB6\xC3\xBC\xC3\xA4\xC3\xB6\xC3\xBC\xC3\xA4\xC3\xB6\xC3\xBC\xC3\xA4\xC3\xB6\xC3\xBC\xC3\xA4\xC3\xB6\xC3\xBC\xC3\xA4\xC3\xB6\xC3\xBC\xC3\xA4\xC3\xB6\xC3\xBC\xC3\xA4\xC3\xB6\xC3\xBC";
    const fs::path umlautFile = out / fs::path(std::u8string(umlauts.begin(), umlauts.end()) + u8".nyxlayout");
    writeAll(umlautFile, readAll(out / "Projector night.nyxlayout"));
    CHECK(ws::importFile(other, umlautFile, &name).ok);
    CHECK(ws::importFile(other, umlautFile, &name).ok);
    CHECK(validUtf8(name));
    CHECK(name.size() <= ws::kMaxNameBytes);
    CHECK(name.substr(name.size() - 2) == " 2");

    const size_t before = ws::list(other).size();
    CHECK(!ws::importFile(other, out / "nothing.nyxlayout").ok);
    CHECK(!ws::importFile(other, out).ok);
    writeAll(out / "garbage.nyxlayout", "not a layout at all");
    const ws::Result garbage = ws::importFile(other, out / "garbage.nyxlayout");
    CHECK(!garbage.ok);
    CHECK(!garbage.error.empty());
    writeAll(out / "plain.nyxlayout", kLayout);
    CHECK(!ws::importFile(other, out / "plain.nyxlayout").ok);
    std::string newer = readAll(out / "Projector night.nyxlayout");
    newer.replace(newer.find("format=1"), 8, "format=9");
    writeAll(out / "newer.nyxlayout", newer);
    const ws::Result tooNew = ws::importFile(other, out / "newer.nyxlayout");
    CHECK(!tooNew.ok);
    CHECK(tooNew.error.find("newer version") != std::string::npos);
    std::string huge = readAll(out / "Projector night.nyxlayout");
    huge += std::string(ws::kMaxFileBytes + 2048, ' ');
    writeAll(out / "huge.nyxlayout", huge);
    CHECK(!ws::importFile(other, out / "huge.nyxlayout").ok);
    writeAll(out / "CON.nyxlayout", readAll(out / "Projector night.nyxlayout"));
    CHECK(ws::importFile(other, out / "CON.nyxlayout", &name).ok);
    CHECK(name == "Imported layout");
    CHECK(ws::list(other).size() == before + 1);
    for (const ws::Entry& entry : ws::list(other)) {
        CHECK(entry.valid);
    }

    const fs::path crowded = scratch("share-crowded");
    for (size_t i = 0; i < ws::kMaxWorkspaces; ++i) {
        CHECK(ws::save(crowded, "Layout " + std::to_string(i), kLayout, 100, 100, "x").ok);
    }
    const ws::Result full = ws::importFile(crowded, out / "Projector night.nyxlayout");
    CHECK(!full.ok);
    CHECK(ws::list(crowded).size() == ws::kMaxWorkspaces);

    std::string crlf = readAll(out / "Projector night.nyxlayout");
    std::string windows;
    for (const char c : crlf) {
        if (c == '\n') {
            windows += '\r';
        }
        windows += c;
    }
    writeAll(out / "Windows lines.nyxlayout", windows);
    CHECK(ws::importFile(other, out / "Windows lines.nyxlayout", &name).ok);
    CHECK(name == "Windows lines");
    CHECK(ws::load(other, "Windows lines", layout).ok);

    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::remove_all(out, ec);
    fs::remove_all(other, ec);
    fs::remove_all(crowded, ec);
}

void testStoreLimitsAndDamage() {
    const fs::path dir = scratch("limits");
    for (size_t i = 0; i < ws::kMaxWorkspaces; ++i) {
        CHECK(ws::save(dir, "Layout " + std::to_string(i), kLayout, 100, 100, "x").ok);
    }
    const ws::Result tooMany = ws::save(dir, "One more", kLayout, 100, 100, "x");
    CHECK(!tooMany.ok);
    CHECK(!tooMany.error.empty());
    CHECK(ws::save(dir, "layout 3", kLayout, 100, 100, "x").ok);
    CHECK(ws::list(dir).size() == ws::kMaxWorkspaces);
    writeAll(ws::folder(dir) / "Layout 5.ini", "[Window][broken\nPos=");
    writeAll(ws::folder(dir) / "notes.txt", "not a workspace");
    writeAll(ws::folder(dir) / "Layout 6.ini.tmp", "left over");
    int damaged = 0;
    for (const ws::Entry& entry : ws::list(dir)) {
        if (!entry.valid) {
            ++damaged;
            CHECK(entry.name == "Layout 5");
            CHECK(!entry.problem.empty());
        }
    }
    CHECK(damaged == 1);
    std::string layout;
    CHECK(!ws::load(dir, "Layout 5", layout).ok);
    CHECK(layout.empty());
    CHECK(ws::remove(dir, "Layout 5").ok);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

void testQuarantine() {
    const fs::path dir = scratch("quarantine");
    const fs::path file = dir / "layout.ini";
    CHECK(ws::quarantine(file));
    writeAll(file, "first");
    CHECK(ws::quarantine(file));
    CHECK(!fs::exists(file));
    CHECK(readAll(dir / "layout.ini.bad") == "first");
    writeAll(file, "second");
    CHECK(ws::quarantine(file));
    CHECK(!fs::exists(file));
    CHECK(readAll(dir / "layout.ini.bad") == "second");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

void testDefaultShortcuts() {
    sc::Table table;
    CHECK(table.isDefault());
    CHECK(sc::format(table.get(sc::Action::Tap)) == "Space");
    CHECK(sc::format(table.get(sc::Action::Blackout)) == "B");
    CHECK(sc::format(table.get(sc::Action::Fullscreen)) == "F11");
    CHECK(sc::format(table.get(sc::Action::Undo)) == "Ctrl+Z");
    CHECK(sc::format(table.get(sc::Action::Redo)) == "Ctrl+Y");
    CHECK(sc::format(table.get(sc::Action::Settings)) == "Ctrl+,");
    CHECK(sc::format(table.get(sc::Action::Record)) == "Ctrl+Shift+R");
    CHECK(sc::format(table.get(sc::Action::Manual)) == "F1");
    CHECK(sc::format(table.get(sc::Action::NextDeck)) == "PageDown");
    CHECK(sc::format(table.get(sc::Action::PreviousDeck)) == "PageUp");
    CHECK(sc::format(table.get(sc::Action::InterfaceLarger)) == "Ctrl+=");
    CHECK(sc::format(table.get(sc::Action::InterfaceSmaller)) == "Ctrl+-");
    CHECK(sc::format(table.get(sc::Action::InterfaceReset)) == "Ctrl+0");
    for (int i = 0; i < sc::kActionCount; ++i) {
        const sc::Action action = static_cast<sc::Action>(i);
        CHECK(!table.get(action).empty());
        CHECK(sc::reservedReason(table.get(action)).empty());
        CHECK(table.find(table.get(action)) == i);
        CHECK(sc::actionIndex(sc::info(action).id) == i);
        CHECK(std::string(sc::info(action).label).size() > 2);
        sc::Binding again;
        std::string error;
        CHECK(sc::parse(sc::format(table.get(action)), again, error));
        CHECK(again == table.get(action));
    }
    CHECK(sc::actionIndex("nothing") == -1);
    CHECK(sc::actionIndex("") == -1);
}

void testParse() {
    sc::Binding b;
    std::string error;
    CHECK(sc::parse("ctrl+shift+r", b, error));
    CHECK(b.key == "R" && b.ctrl && b.shift && !b.alt);
    CHECK(sc::parse("Ctrl+,", b, error));
    CHECK(b.key == "Comma" && b.ctrl);
    CHECK(sc::parse("Ctrl+Comma", b, error));
    CHECK(b.key == "Comma");
    CHECK(sc::parse("  alt + f11  ", b, error));
    CHECK(b.key == "F11" && b.alt && !b.ctrl);
    CHECK(sc::parse("Control+=", b, error));
    CHECK(b.key == "Equal" && b.ctrl);
    CHECK(sc::parse("pagedown", b, error));
    CHECK(b.key == "PageDown");
    CHECK(sc::parse("", b, error));
    CHECK(b.empty());
    CHECK(sc::parse("   ", b, error));
    CHECK(b.empty());
    CHECK(!sc::parse("Ctrl+", b, error));
    CHECK(error == "A shortcut needs a key");
    CHECK(!sc::parse("Foo+R", b, error));
    CHECK(error.find("Foo") != std::string::npos);
    CHECK(!sc::parse("Ctrl+Ctrl+R", b, error));
    CHECK(!sc::parse("Ctrl+Escape", b, error));
    CHECK(!sc::parse("Ctrl+F13", b, error));
    CHECK(!sc::parse("++", b, error));
    CHECK(b.empty());
}

void testKeys() {
    CHECK(sc::keyNames().size() == 60);
    CHECK(sc::knownKey("A"));
    CHECK(sc::knownKey("F12"));
    CHECK(sc::knownKey("Space"));
    CHECK(sc::knownKey("PageUp"));
    CHECK(!sc::knownKey("F13"));
    CHECK(!sc::knownKey("a"));
    CHECK(!sc::knownKey("Escape"));
    CHECK(!sc::knownKey("Tab"));
    CHECK(!sc::knownKey("Enter"));
    CHECK(!sc::knownKey("Delete"));
    CHECK(!sc::knownKey("Backspace"));
    CHECK(!sc::knownKey("LeftArrow"));
    CHECK(sc::keyLabel("Comma") == ",");
    CHECK(sc::keyLabel("Equal") == "=");
    CHECK(sc::keyLabel("R") == "R");
}

void testSet() {
    sc::Table table;
    sc::SetResult r = table.set(sc::Action::Blackout, sc::Binding{"Space", false, false, false});
    CHECK(!r.ok);
    CHECK(r.conflict == static_cast<int>(sc::Action::Tap));
    CHECK(sc::format(table.get(sc::Action::Blackout)) == "B");
    CHECK(table.set(sc::Action::Blackout, sc::Binding{"K", false, false, false}).ok);
    CHECK(sc::format(table.get(sc::Action::Blackout)) == "K");
    CHECK(!table.isDefault());
    CHECK(!table.isDefault(sc::Action::Blackout));
    CHECK(table.isDefault(sc::Action::Tap));
    CHECK(table.set(sc::Action::Tap, sc::Binding{"B", false, false, false}).ok);
    CHECK(table.set(sc::Action::Tap, table.get(sc::Action::Tap)).ok);
    CHECK(!table.set(sc::Action::Redo, sc::Binding{"1", false, false, false}).ok);
    CHECK(!table.set(sc::Action::Redo, sc::Binding{"3", true, false, false}).ok);
    CHECK(!table.set(sc::Action::Redo, sc::Binding{"8", true, true, false}).ok);
    CHECK(table.set(sc::Action::Redo, sc::Binding{"1", false, false, true}).ok);
    CHECK(table.set(sc::Action::Redo, sc::Binding{"9", false, false, false}).ok);
    CHECK(table.set(sc::Action::Redo, sc::Binding{"1", false, true, false}).ok);
    CHECK(!table.set(sc::Action::Redo, sc::Binding{"Z", true, true, false}).ok);
    CHECK(!table.set(sc::Action::Redo, sc::Binding{"F4", false, false, true}).ok);
    CHECK(!table.set(sc::Action::Redo, sc::Binding{"Escape", false, false, false}).ok);
    CHECK(table.set(sc::Action::Redo, sc::Binding{}).ok);
    CHECK(table.get(sc::Action::Redo).empty());
    CHECK(sc::format(table.get(sc::Action::Redo)).empty());
    CHECK(table.find(sc::Binding{}) == -1);
    table.reset(sc::Action::Redo);
    CHECK(sc::format(table.get(sc::Action::Redo)) == "Ctrl+Y");
    table.resetAll();
    CHECK(table.isDefault());
}

void testMatches() {
    sc::Table table;
    CHECK(table.matches(sc::Action::Blackout, "B", false, false, false));
    CHECK(!table.matches(sc::Action::Blackout, "B", true, false, false));
    CHECK(!table.matches(sc::Action::Blackout, "B", false, true, false));
    CHECK(!table.matches(sc::Action::Blackout, "C", false, false, false));
    CHECK(table.matches(sc::Action::Record, "R", true, true, false));
    CHECK(!table.matches(sc::Action::Record, "R", true, false, false));
    table.set(sc::Action::Blackout, sc::Binding{});
    CHECK(!table.matches(sc::Action::Blackout, "", false, false, false));
}

void testJson() {
    sc::Table table;
    table.set(sc::Action::Blackout, sc::Binding{"K", true, false, false});
    table.set(sc::Action::Manual, sc::Binding{});
    const std::string text = table.toJson();
    sc::Table back;
    const sc::LoadResult result = back.fromJson(text);
    CHECK(result.state == sc::LoadState::Loaded);
    CHECK(result.notes.empty());
    for (int i = 0; i < sc::kActionCount; ++i) {
        CHECK(back.get(static_cast<sc::Action>(i)) == table.get(static_cast<sc::Action>(i)));
    }
    CHECK(text.find("\"version\": 1") != std::string::npos);
    CHECK(text.find("\"blackout\": \"Ctrl+K\"") != std::string::npos);
    CHECK(text.find("\"manual\": \"\"") != std::string::npos);

    sc::Table mixed;
    const sc::LoadResult partial = mixed.fromJson("{\"version\":1,\"bindings\":{\"blackout\":\"Ctrl+Q\",\"tap\":\"Ctrl+Bogus\",\"nothing\":\"X\",\"undo\":5}}");
    CHECK(partial.state == sc::LoadState::Loaded);
    CHECK(partial.notes.size() == 2);
    CHECK(sc::format(mixed.get(sc::Action::Blackout)) == "Ctrl+Q");
    CHECK(sc::format(mixed.get(sc::Action::Tap)) == "Space");
    CHECK(sc::format(mixed.get(sc::Action::Undo)) == "Ctrl+Z");
    CHECK(sc::format(mixed.get(sc::Action::Fullscreen)) == "F11");

    sc::Table clash;
    const sc::LoadResult clashed = clash.fromJson("{\"version\":1,\"bindings\":{\"tap\":\"B\",\"blackout\":\"B\"}}");
    CHECK(clashed.state == sc::LoadState::Loaded);
    CHECK(sc::format(clash.get(sc::Action::Tap)) == "B");
    CHECK(clash.get(sc::Action::Blackout).empty());
    CHECK(clashed.notes.size() >= 1);
    for (int i = 0; i < sc::kActionCount; ++i) {
        const sc::Binding& binding = clash.get(static_cast<sc::Action>(i));
        CHECK(binding.empty() || clash.find(binding) == i);
    }

    sc::Table reservedFile;
    const sc::LoadResult reserved = reservedFile.fromJson("{\"version\":1,\"bindings\":{\"redo\":\"Ctrl+1\"}}");
    CHECK(reserved.state == sc::LoadState::Loaded);
    CHECK(sc::format(reservedFile.get(sc::Action::Redo)) == "Ctrl+Y");

    sc::Table keep;
    keep.set(sc::Action::Tap, sc::Binding{"T", false, false, false});
    const sc::LoadResult newer = keep.fromJson("{\"version\":2,\"bindings\":{}}");
    CHECK(newer.state == sc::LoadState::Newer);
    CHECK(sc::format(keep.get(sc::Action::Tap)) == "T");
    CHECK(keep.fromJson("not json").state == sc::LoadState::Damaged);
    CHECK(keep.fromJson("[]").state == sc::LoadState::Damaged);
    CHECK(keep.fromJson("{\"bindings\":{}}").state == sc::LoadState::Damaged);
    CHECK(keep.fromJson("{\"version\":1}").state == sc::LoadState::Damaged);
    CHECK(keep.fromJson("{\"version\":\"1\",\"bindings\":{}}").state == sc::LoadState::Damaged);
    CHECK(sc::format(keep.get(sc::Action::Tap)) == "T");
    sc::Table empty;
    CHECK(empty.fromJson("{\"version\":1,\"bindings\":{}}").state == sc::LoadState::Loaded);
    CHECK(empty.isDefault());
}

void testShortcutFiles() {
    const fs::path dir = scratch("shortcuts");
    const fs::path file = dir / "shortcuts.json";
    sc::Table table;
    sc::LoadResult missing = sc::loadFile(file, table);
    CHECK(missing.state == sc::LoadState::Missing);
    CHECK(table.isDefault());
    table.set(sc::Action::Fullscreen, sc::Binding{"F10", false, false, false});
    CHECK(sc::saveFile(file, table));
    CHECK(!fs::exists(dir / "shortcuts.json.tmp"));
    sc::Table back;
    CHECK(sc::loadFile(file, back).state == sc::LoadState::Loaded);
    CHECK(sc::format(back.get(sc::Action::Fullscreen)) == "F10");
    writeAll(file, "{ broken");
    sc::Table untouched;
    untouched.set(sc::Action::Tap, sc::Binding{"T", false, false, false});
    CHECK(sc::loadFile(file, untouched).state == sc::LoadState::Damaged);
    CHECK(sc::format(untouched.get(sc::Action::Tap)) == "T");
    writeAll(file, std::string(70 * 1024, 'x'));
    CHECK(sc::loadFile(file, untouched).state == sc::LoadState::Damaged);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

void testContrast() {
    using nvj::contrast::Rgb;
    const Rgb black{0.0f, 0.0f, 0.0f};
    const Rgb white{1.0f, 1.0f, 1.0f};
    CHECK(std::fabs(nvj::contrast::ratio(black, white) - 21.0) < 0.001);
    CHECK(std::fabs(nvj::contrast::ratio(white, black) - 21.0) < 0.001);
    CHECK(std::fabs(nvj::contrast::ratio(white, white) - 1.0) < 0.001);
    CHECK(std::fabs(nvj::contrast::luminance(Rgb{0.5f, 0.5f, 0.5f}) - 0.2140) < 0.001);
    CHECK(std::fabs(nvj::contrast::luminance(Rgb{1.0f, 0.0f, 0.0f}) - 0.2126) < 0.0001);
    CHECK(std::fabs(nvj::contrast::ratio(Rgb{0.467f, 0.467f, 0.467f}, white) - 4.48) < 0.05);
    CHECK(nvj::contrast::ratio(Rgb{0.4f, 0.4f, 0.4f}, white) > nvj::contrast::kNormalText);
    CHECK(nvj::contrast::ratio(Rgb{0.5f, 0.5f, 0.5f}, white) < nvj::contrast::kNormalText);
    const Rgb half = nvj::contrast::blend(white, 0.5f, black);
    CHECK(std::fabs(half.r - 0.5f) < 0.0001f);
    const Rgb same = nvj::contrast::blend(white, 0.0f, black);
    CHECK(same.r == 0.0f);
    const Rgb clamped = nvj::contrast::blend(white, 2.0f, black);
    CHECK(clamped.r == 1.0f);
    CHECK(nvj::contrast::luminance(Rgb{2.0f, 2.0f, 2.0f}) <= 1.0001);
}

}

void useDataDir(const fs::path& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.string().c_str(), 1);
#endif
}

void testInterfaceSettings() {
    const fs::path dir = scratch("settings");
    useDataDir(dir);

    nvj::Settings defaults;
    CHECK(!defaults.uiAutoScale);
    CHECK(defaults.uiContrast == "standard");
    CHECK(!defaults.uiKeyboardNav);
    CHECK(defaults.uiScale == 1.0f);

    CHECK(nvj::saveSettings(defaults));
    const std::string plain = readAll(dir / "settings.json");
    CHECK(plain.find("uiAutoScale") == std::string::npos);
    CHECK(plain.find("uiContrast") == std::string::npos);
    CHECK(plain.find("uiKeyboardNav") == std::string::npos);

    nvj::Settings custom;
    custom.uiScale = 2.25f;
    custom.uiAutoScale = true;
    custom.uiContrast = "high";
    custom.uiKeyboardNav = true;
    CHECK(nvj::saveSettings(custom));
    nvj::Settings back;
    CHECK(nvj::loadSettings(back));
    CHECK(back.uiScale == 2.25f);
    CHECK(back.uiAutoScale);
    CHECK(back.uiContrast == "high");
    CHECK(back.uiKeyboardNav);

    custom.uiScale = 9.0f;
    custom.uiContrast = "neon";
    CHECK(nvj::saveSettings(custom));
    CHECK(nvj::loadSettings(back));
    CHECK(back.uiScale == 2.5f);
    CHECK(back.uiContrast == "standard");

    writeAll(dir / "settings.json", "{\"uiScale\": 0.1, \"uiContrast\": 7, \"uiAutoScale\": \"yes\", \"uiKeyboardNav\": 1}");
    nvj::Settings odd;
    CHECK(nvj::loadSettings(odd));
    CHECK(odd.uiScale == 0.7f);
    CHECK(odd.uiContrast == "standard");
    CHECK(!odd.uiAutoScale);
    CHECK(!odd.uiKeyboardNav);

    writeAll(dir / "settings.json", "{\"uiScale\": 1.5}");
    nvj::Settings older;
    CHECK(nvj::loadSettings(older));
    CHECK(older.uiScale == 1.5f);
    CHECK(!older.uiAutoScale);
    CHECK(older.uiContrast == "standard");
    CHECK(!older.uiKeyboardNav);
}

int main() {
    testInterfaceSettings();
    testNames();
    testReservedAndKeys();
    testLayoutCheck();
    testFileHeader();
    testStore();
    testStoreLimitsAndDamage();
    testShare();
    testQuarantine();
    testDefaultShortcuts();
    testParse();
    testKeys();
    testSet();
    testMatches();
    testJson();
    testShortcutFiles();
    testContrast();
    return testing::finish();
}
