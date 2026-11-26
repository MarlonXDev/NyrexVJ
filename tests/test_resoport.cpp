#include "check.hpp"

#include "core/resoport.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace nvj::resoport;

const char* kFixture = R"xml(<?xml version='1.0' encoding='utf-8'?>
<!-- made for the test -->
<Composition name="Club &amp; Show &#228;&#x4E2D;" uniqueId="1" numDecks="3" numLayers="5" numColumns="9">
	<versionInfo name="Resolume Arena" majorVersion="7" minorVersion="22" microVersion="4" />
	<CompositionInfo name="Club" description="" width="1280" height="720">
		<DeckInfo name="Opener" id="1" closed="0" />
	</CompositionInfo>
	<TempoController name="TempoController">
		<Params name="Params">
			<ParamRange name="Tempo" T="DOUBLE" default="120" value="132.5" />
		</Params>
	</TempoController>
	<Layer name="Layer" layerIndex="0">
		<VideoTrack name="VideoTrack">
			<RenderPass name="RenderPassChain">
				<RenderPass name="Blur" type="BlurEffect" baseType="Effect" />
				<RenderPass name="Transform" type="TransformEffect" baseType="Effect" />
			</RenderPass>
			<ChoosableMixer name="Blend">
				<RenderPass name="Add" type="Add" baseType="Mixer">
					<Params name="Params"><ParamRange name="Opacity" T="DOUBLE" default="1" value="0.5" /></Params>
				</RenderPass>
			</ChoosableMixer>
			<ChoosableMixer name="Transition"><RenderPass name="Alpha" type="Alpha" baseType="Mixer" /></ChoosableMixer>
		</VideoTrack>
	</Layer>
	<Layer name="Layer" layerIndex="1">
		<VideoTrack name="VideoTrack">
			<ChoosableMixer name="Blend"><RenderPass name="Alpha" type="Alpha" baseType="Mixer" /></ChoosableMixer>
		</VideoTrack>
	</Layer>
	<Layer name="Layer" layerIndex="2">
		<VideoTrack name="VideoTrack">
			<ChoosableMixer name="Blend"><RenderPass name="Soft Light" type="Soft Light" baseType="Mixer" /></ChoosableMixer>
		</VideoTrack>
	</Layer>
	<Layer name="Layer" layerIndex="3">
		<VideoTrack name="VideoTrack">
			<ChoosableMixer name="Blend"><RenderPass name="Plasma Mix" type="PlasmaMix" baseType="Mixer" /></ChoosableMixer>
		</VideoTrack>
	</Layer>
	<Layer name="Layer" layerIndex="4">
		<VideoTrack name="VideoTrack">
			<ChoosableMixer name="Blend"><RenderPass name="Alpha" type="Alpha" baseType="Mixer" /></ChoosableMixer>
		</VideoTrack>
	</Layer>
	<Deck name="Deck" uniqueId="10">
		<Params name="Params"><Param name="Name" T="STRING" value="Opener" /></Params>
		<Column columnIndex="0" />
		<Clip name="Clip" layerIndex="0" columnIndex="0">
			<PreloadData><VideoFile value="C:\Media\Loops\Tunnel.MP4" /></PreloadData>
			<Params name="Params"><Param name="Name" T="STRING" value="Tunnel" /></Params>
			<VideoTrack name="VideoTrack"><PrimarySource><VideoSource name="VideoSource" type="VideoFormatReaderSource" fileName="C:\Media\Loops\Tunnel.MP4" /></PrimarySource></VideoTrack>
		</Clip>
		<Clip name="Clip" layerIndex="1" columnIndex="2">
			<Params name="Params"><Param name="Name" T="STRING" value="Logo" /></Params>
			<VideoTrack name="VideoTrack"><PrimarySource><VideoSource type="VideoFormatReaderSource" fileName="/Users/dj/Pictures/logo.png" /></PrimarySource></VideoTrack>
		</Clip>
		<Clip name="Clip" layerIndex="2" columnIndex="1">
			<VideoTrack name="VideoTrack"><PrimarySource><VideoSource type="GeneratorVideoSource" /></PrimarySource></VideoTrack>
		</Clip>
		<Clip name="Clip" layerIndex="3" columnIndex="3">
			<VideoTrack name="VideoTrack"><PrimarySource><VideoSource type="CompositionRouterVideoSource" /></PrimarySource></VideoTrack>
		</Clip>
		<Clip name="Clip" layerIndex="0" columnIndex="4">
			<VideoTrack name="VideoTrack"><PrimarySource><VideoSource type="VideoFormatReaderSource" fileName="D:\gone\vanished.mov" /></PrimarySource></VideoTrack>
		</Clip>
		<Clip name="Clip" layerIndex="0" columnIndex="5">
			<VideoTrack name="VideoTrack"><PrimarySource><VideoSource type="VideoFormatReaderSource" fileName="D:\media\clip.dxv" /></PrimarySource></VideoTrack>
		</Clip>
		<Clip name="Clip" layerIndex="0" columnIndex="8">
			<VideoTrack name="VideoTrack"><PrimarySource><VideoSource type="VideoFormatReaderSource" fileName="C:\Media\Loops\Tunnel.MP4" /></PrimarySource></VideoTrack>
		</Clip>
		<Clip name="Clip" layerIndex="4" columnIndex="0">
			<VideoTrack name="VideoTrack"><PrimarySource><VideoSource type="VideoFormatReaderSource" fileName="C:\Media\Loops\Tunnel.MP4" /></PrimarySource></VideoTrack>
		</Clip>
		<Clip name="Clip" layerIndex="1" columnIndex="7">
			<VideoTrack name="VideoTrack"><PrimarySource /></VideoTrack>
		</Clip>
	</Deck>
	<Deck name="Deck" uniqueId="11">
		<Params name="Params"><Param name="Name" T="STRING" value="Second" /></Params>
	</Deck>
	<Deck name="Deck" uniqueId="12">
		<Params name="Params"><Param name="Name" T="STRING" value="Late" /></Params>
		<Clip name="Clip" layerIndex="3" columnIndex="7">
			<VideoTrack name="VideoTrack"><PrimarySource><VideoSource type="VideoFormatReaderSource" fileName="C:\Media\extra.mp4" /></PrimarySource></VideoTrack>
		</Clip>
	</Deck>
</Composition>
)xml";

fs::path scratch(const std::string& name) {
    const fs::path path = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
    return path;
}

void touch(const fs::path& file) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    out << "x";
}

std::string text(const fs::path& path) {
    return std::string(reinterpret_cast<const char*>(path.u8string().c_str()));
}

void testXml() {
    XmlDocument xml;
    std::string error;
    CHECK(parseXml("<a x='1' y=\"two &amp; &lt;3\"><b/><c>text</c></a>", xml, error));
    CHECK(xml.nodes.size() == 3 && xml.nodes[0].tag == "a" && xml.nodes[0].children.size() == 2);
    CHECK(xml.nodes[0].attribute("y") && *xml.nodes[0].attribute("y") == "two & <3");
    CHECK(xml.nodes[0].attribute("missing") == nullptr);
    CHECK(xml.nodes[1].parent == 0 && xml.nodes[2].parent == 0);

    CHECK(parseXml("\xEF\xBB\xBF<?xml version='1.0'?><!DOCTYPE a><a><![CDATA[ <not> ]]></a>", xml, error));
    CHECK(xml.nodes.size() == 1);
    CHECK(parseXml("<a v='&#65;&#x42;&#xE4;&bogus;&'/>", xml, error));
    CHECK(*xml.nodes[0].attribute("v") == "AB\xC3\xA4&bogus;&");
    CHECK(parseXml("<a v='&#0;&#xD800;&#x110000;'/>", xml, error));
    CHECK(*xml.nodes[0].attribute("v") == "???");
    CHECK(parseXml("<a k='1' k='2'/>", xml, error) && *xml.nodes[0].attribute("k") == "1");

    const char* broken[] = {
        "",
        "   ",
        "text only",
        "<a>",
        "<a></b>",
        "<a><b></a></b>",
        "</a>",
        "<a x=1/>",
        "<a x='1/>",
        "<a x/>",
        "<a><",
        "<a b='1' c",
        "<a/><b/>",
        "<!DOCTYPE a [<!ENTITY x 'y'>]><a/>",
        "<a><!-- never closed </a>",
        "<a><![CDATA[ never closed </a>",
        "<?xml never closed <a/>",
        "<!ELEMENT a>",
        "<1a/>",
        "<a></a ",
        "\xFF\xFE<\0a\0/\0>\0",
    };
    for (const char* item : broken) {
        CHECK(!parseXml(std::string(item, std::char_traits<char>::length(item)), xml, error));
        CHECK(!error.empty());
        CHECK(xml.nodes.empty());
    }

    std::string deep;
    for (int i = 0; i < kMaxDepth + 5; ++i) {
        deep += "<a>";
    }
    CHECK(!parseXml(deep, xml, error));
    std::string allowed;
    for (int i = 0; i < kMaxDepth; ++i) {
        allowed += "<a>";
    }
    for (int i = 0; i < kMaxDepth; ++i) {
        allowed += "</a>";
    }
    CHECK(parseXml(allowed, xml, error));

    std::string wide = "<a";
    for (int i = 0; i < static_cast<int>(kMaxAttributes) + 1; ++i) {
        wide += " k" + std::to_string(i) + "='v'";
    }
    wide += "/>";
    CHECK(!parseXml(wide, xml, error));

    std::string longValue = "<a v='" + std::string(kMaxValueLength + 10, 'x') + "' w='ok'/>";
    CHECK(parseXml(longValue, xml, error));
    CHECK(xml.nodes[0].attribute("v")->empty() && *xml.nodes[0].attribute("w") == "ok");
}

void testRobustness() {
    const std::string fixture(kFixture);
    Document doc;
    std::string error;
    CHECK(parseDocument(fixture, doc, error));
    for (size_t length = 0; length < fixture.size(); length += 3) {
        Document cut;
        std::string cutError;
        parseDocument(fixture.substr(0, length), cut, cutError);
    }
    uint32_t state = 12345;
    int accepted = 0;
    for (int round = 0; round < 3000; ++round) {
        std::string mutated = fixture;
        for (int k = 0; k < 4; ++k) {
            state = state * 1664525u + 1013904223u;
            const size_t at = (state >> 8) % mutated.size();
            state = state * 1664525u + 1013904223u;
            mutated[at] = static_cast<char>(state >> 16);
        }
        Document other;
        std::string otherError;
        if (parseDocument(mutated, other, otherError)) {
            ++accepted;
        }
    }
    CHECK(accepted > 0);
}

void testDocument() {
    Document doc;
    std::string error;
    CHECK(!parseDocument("<Other/>", doc, error) && !error.empty());
    CHECK(!parseDocument("<Composition", doc, error));

    CHECK(parseDocument(kFixture, doc, error));
    CHECK(doc.name == "Club & Show \xC3\xA4\xE4\xB8\xAD");
    CHECK(doc.application == "Resolume Arena" && doc.versionMajor == 7 && doc.versionMinor == 22);
    CHECK(doc.width == 1280 && doc.height == 720);
    CHECK(doc.declaredDecks == 3 && doc.declaredLayers == 5 && doc.declaredColumns == 9);
    CHECK(doc.bpm == 132.5f);
    CHECK(doc.effectPasses == 1);
    CHECK(doc.layers.size() == 5);
    CHECK(doc.layers[0].blend == "Add" && doc.layers[0].opacity == 0.5f);
    CHECK(doc.layers[1].blend == "Alpha" && doc.layers[1].opacity == 1.0f);
    CHECK(doc.layers[2].blend == "Soft Light");
    CHECK(doc.decks.size() == 3 && doc.decks[0].name == "Opener" && doc.decks[1].name == "Second" && doc.decks[2].name == "Late");
    CHECK(doc.clips.size() == 9);
    CHECK(doc.clips[0].source == SourceKind::Media && doc.clips[0].path == "C:\\Media\\Loops\\Tunnel.MP4" && doc.clips[0].name == "Tunnel");
    CHECK(doc.clips[0].deck == 0 && doc.clips[0].layer == 0 && doc.clips[0].column == 0);
    CHECK(doc.clips[1].path == "/Users/dj/Pictures/logo.png" && doc.clips[1].layer == 1 && doc.clips[1].column == 2);
    CHECK(doc.clips[2].source == SourceKind::Generator);
    CHECK(doc.clips[3].source == SourceKind::Router);
    CHECK(doc.clips.back().deck == 2);

    CHECK(parseDocument("<Composition><Layer><VideoTrack><ChoosableMixer name='Blend'><RenderPass type='Add'><Params><ParamRange name='Opacity' value='7'/></Params></RenderPass></ChoosableMixer></VideoTrack></Layer></Composition>", doc, error));
    CHECK(doc.layers.size() == 1 && doc.layers[0].opacity == 1.0f && doc.layers[0].blend == "Add");
    CHECK(parseDocument("<Composition><Layer><VideoTrack><ChoosableMixer name='Blend'><RenderPass type='Add'><Params><ParamRange name='Opacity' value='nan'/></Params></RenderPass></ChoosableMixer></VideoTrack></Layer></Composition>", doc, error));
    CHECK(doc.layers[0].opacity == 1.0f);
    CHECK(parseDocument("<Composition><TempoController><Params><Param name='BPM' value='9999'/></Params></TempoController></Composition>", doc, error));
    CHECK(doc.bpm == 0.0f);

    Document missing;
    CHECK(!loadDocument((fs::temp_directory_path() / "resoport_no_such_file.avc").string(), missing, error));
    const fs::path dir = scratch("resoport_load");
    {
        std::ofstream out(dir / "show.avc", std::ios::binary);
        out << kFixture;
    }
    CHECK(loadDocument(text(dir / "show.avc"), missing, error) && missing.clips.size() == 9);
    {
        std::ofstream out(dir / "bad.avc", std::ios::binary);
        out << "not xml at all";
    }
    CHECK(!loadDocument(text(dir / "bad.avc"), missing, error));
}

void testBlendNames() {
    bool known = false;
    CHECK(blendModeFor("Alpha", known) == nvj::BlendNormal && known);
    CHECK(blendModeFor("", known) == nvj::BlendNormal && known);
    CHECK(blendModeFor("Add", known) == nvj::BlendAdd && known);
    CHECK(blendModeFor("soft light", known) == nvj::BlendSoftLight && known);
    CHECK(blendModeFor("Soft_Light", known) == nvj::BlendSoftLight && known);
    CHECK(blendModeFor("HardLight", known) == nvj::BlendHardLight && known);
    CHECK(blendModeFor("Color Dodge", known) == nvj::BlendColorDodge && known);
    CHECK(blendModeFor("Linear Burn", known) == nvj::BlendLinearBurn && known);
    CHECK(blendModeFor("Multiply", known) == nvj::BlendMultiply && known);
    CHECK(blendModeFor("Plasma Mix", known) == nvj::BlendNormal && !known);
}

void testFinder() {
    const fs::path root = scratch("resoport_media");
    touch(root / "show.avc");
    touch(root / "Tunnel.mp4");
    touch(root / "assets" / "deep" / "Logo.PNG");
    touch(root / "notes.txt");
    const fs::path extra = scratch("resoport_extra");
    touch(extra / "far" / "extra.mp4");
    touch(extra / "direct.mov");

    MediaFinder finder(text(root / "show.avc"), {text(extra)});
    CHECK(finder.find("C:\\Media\\Loops\\tunnel.mp4") == text(root / "Tunnel.mp4"));
    CHECK(finder.find("C:\\Media\\Loops\\Tunnel.mp4") == text(root / "Tunnel.mp4"));
    CHECK(finder.find("/Users/dj/Pictures/logo.png") == text(root / "assets" / "deep" / "Logo.PNG"));
    CHECK(finder.find("C:\\Media\\extra.mp4") == text(extra / "far" / "extra.mp4"));
    CHECK(finder.find(text(extra / "direct.mov")) == text(extra / "direct.mov"));
    CHECK(finder.find("assets/deep/Logo.PNG") == text(root / "assets" / "deep" / "Logo.PNG"));
    CHECK(finder.find("../secret.mp4").empty());
    CHECK(finder.find("nothing.mp4").empty());
    CHECK(finder.find("").empty());
    CHECK(finder.find("C:\\Media\\").empty());
    CHECK(finder.find("notes.txt").empty());
    CHECK(finder.find("file://" + text(root / "Tunnel.mp4")) == text(root / "Tunnel.mp4"));

    MediaFinder withoutExtra(text(root / "show.avc"), {});
    CHECK(withoutExtra.find("C:\\Media\\extra.mp4").empty());
    MediaFinder noRoots("", {"/definitely/not/here"});
    CHECK(noRoots.find("anything.mp4").empty());
}

void testBuild() {
    const fs::path root = scratch("resoport_build");
    touch(root / "show.avc");
    touch(root / "Tunnel.mp4");
    touch(root / "logo.png");
    touch(root / "extra.mp4");

    Document doc;
    std::string error;
    CHECK(parseDocument(kFixture, doc, error));
    MediaFinder finder(text(root / "show.avc"), {});
    const std::vector<ResolvedClip> resolved = resolveClips(doc, finder);
    CHECK(resolved.size() == doc.clips.size());
    CHECK(resolved[0].file == text(root / "Tunnel.mp4") && !resolved[0].missing);
    CHECK(resolved[1].file == text(root / "logo.png"));
    CHECK(resolved[2].file.empty() && !resolved[2].missing && !resolved[2].unsupported);
    CHECK(resolved[4].missing);
    CHECK(resolved[5].unsupported && !resolved[5].missing);

    int calls = 0;
    nvj::resoport::Report report;
    const nvj::Composition comp = buildComposition(doc, resolved, [&](const std::string& file) {
        ++calls;
        if (file == text(root / "Tunnel.mp4")) {
            return 5;
        }
        if (file == text(root / "logo.png")) {
            return 6;
        }
        if (file == text(root / "extra.mp4")) {
            return 7;
        }
        return -1;
    }, report);

    CHECK(comp.name == "Club & Show \xC3\xA4\xE4\xB8\xAD");
    CHECK(comp.bpm == 132.5f && report.tempoTaken);
    CHECK(comp.resolution == nvj::resolutionIndexFor(1280, 720) && report.resolutionMatched);
    CHECK(comp.activeDeck == 0);
    CHECK(comp.deckUsed[0] && !comp.deckUsed[1] && comp.deckUsed[2]);
    CHECK(comp.deckNames[0] == "Opener" && comp.deckNames[2] == "Late");
    CHECK(comp.layers[0].cells[0] == 5 && comp.layers[1].cells[2] == 6);
    CHECK(comp.layers[0].blend == nvj::BlendAdd && comp.layers[0].opacity == 0.5f);
    CHECK(comp.layers[1].blend == nvj::BlendNormal && comp.layers[1].opacity == 1.0f);
    CHECK(comp.layers[2].blend == nvj::BlendSoftLight);
    CHECK(comp.layers[3].blend == nvj::BlendNormal);
    CHECK(comp.decks[2].layers[3].cells[7] == 7);
    CHECK(comp.decks[2].layers[0].blend == nvj::BlendAdd);

    CHECK(report.clipsImported == 3);
    CHECK(report.clipsMissing == 1 && report.missing.size() == 1 && report.missing[0] == "vanished.mov");
    CHECK(report.clipsUnsupported == 1 && report.unsupported.size() == 1 && report.unsupported[0] == "clip.dxv");
    CHECK(report.clipsGenerator == 1 && report.clipsRouter == 1 && report.clipsOther == 0);
    CHECK(report.clipsSkippedPosition == 2);
    CHECK(report.layersSkipped == 1 && report.decksSkipped == 0 && report.decksImported == 2);
    CHECK(report.effectPasses == 1);
    CHECK(report.layerBlendsMapped == 3 && report.unknownBlends.size() == 1 && report.unknownBlends[0] == "PlasmaMix");
    CHECK(calls == 3);

    nvj::resoport::Report refused;
    const nvj::Composition none = buildComposition(doc, resolved, [](const std::string&) { return -1; }, refused);
    CHECK(refused.clipsImported == 0 && refused.clipsMissing == 4);
    CHECK(none.deckUsed[0] && !none.deckUsed[2]);
    CHECK(none.layers[0].cells[0] == -1);

    Document empty;
    CHECK(parseDocument("<Composition/>", empty, error));
    nvj::resoport::Report emptyReport;
    const nvj::Composition blank = buildComposition(empty, {}, [](const std::string&) { return 1; }, emptyReport);
    CHECK(blank.name == "Resolume composition" && blank.activeDeck == 0 && !emptyReport.tempoTaken);
    CHECK(blank.resolution == nvj::kDefaultResolution && !emptyReport.resolutionMatched);

    Document large = doc;
    large.width = 3840;
    large.height = 2160;
    large.declaredDecks = nvj::kDeckCount + 3;
    large.declaredLayers = 9;
    nvj::resoport::Report largeReport;
    const nvj::Composition fitted = buildComposition(large, resolved, [](const std::string&) { return 2; }, largeReport);
    CHECK(!largeReport.resolutionMatched && fitted.resolution == nvj::kDefaultResolution);
    CHECK(largeReport.decksSkipped == 3 && largeReport.layersSkipped == 5);
}

}

int main() {
    testXml();
    testRobustness();
    testDocument();
    testBlendNames();
    testFinder();
    testBuild();
    return testing::finish();
}
