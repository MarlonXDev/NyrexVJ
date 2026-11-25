#include "control/api_router.hpp"

#include "control/api_page.hpp"
#include "control/control_targets.hpp"
#include "core/preset.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace nvj::api {

using json = nlohmann::json;

namespace {

constexpr int kMaxJsonDepth = 12;
constexpr size_t kMaxPresetNameBytes = 96;

double num(float value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::round(static_cast<double>(value) * 1e6) / 1e6;
}

Reply ready(HttpResponse response) {
    Reply reply;
    reply.response = std::move(response);
    return reply;
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 1;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        parts.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

bool parseIndex(const std::string& text, int low, int high, int& value) {
    if (text.empty() || text.size() > 3) {
        return false;
    }
    int parsed = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        parsed = parsed * 10 + (c - '0');
    }
    if (parsed < low || parsed > high) {
        return false;
    }
    value = parsed;
    return true;
}

std::string joinPath(const std::vector<std::string>& parts, size_t from) {
    std::string out;
    for (size_t i = from; i < parts.size(); ++i) {
        if (i > from) {
            out += '/';
        }
        out += parts[i];
    }
    return out;
}

bool jsonDepthOk(const std::string& text) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > kMaxJsonDepth) {
                return false;
            }
        } else if (c == ']' || c == '}') {
            --depth;
        }
    }
    return true;
}

bool jsonContentType(const HttpRequest& request) {
    const std::string* type = request.header("content-type");
    if (type == nullptr) {
        return false;
    }
    const std::string lower = lowerAscii(*type);
    if (lower.compare(0, 16, "application/json") != 0) {
        return false;
    }
    return lower.size() == 16 || lower[16] == ';' || lower[16] == ' ';
}

bool readBody(const HttpRequest& request, json& out, HttpResponse& error) {
    out = json::object();
    if (request.body.empty()) {
        return true;
    }
    if (!jsonContentType(request)) {
        error = errorResponse(415, "unsupported_media_type", "Send the body as application/json");
        return false;
    }
    if (!jsonDepthOk(request.body)) {
        error = errorResponse(400, "invalid_json", "The JSON is nested too deeply");
        return false;
    }
    json parsed = json::parse(request.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = errorResponse(400, "invalid_json", "The body must be a JSON object");
        return false;
    }
    out = std::move(parsed);
    return true;
}

const char* kindName(ControlKind kind) {
    switch (kind) {
    case ControlKind::Continuous:
        return "continuous";
    case ControlKind::Toggle:
        return "toggle";
    case ControlKind::Trigger:
        return "trigger";
    }
    return "continuous";
}

float valueAt(const Snapshot& snapshot, size_t index) {
    return index < snapshot.controlValues.size() ? snapshot.controlValues[index] : 0.0f;
}

json nameOrNull(const Snapshot& snapshot, int clip) {
    if (snapshot.library && clip >= 0 && static_cast<size_t>(clip) < snapshot.library->clips.size()) {
        return snapshot.library->clips[static_cast<size_t>(clip)].name;
    }
    return nullptr;
}

json slotJson(const Snapshot& snapshot, const SlotInfo& slot, int number) {
    json node;
    node["slot"] = number;
    if (snapshot.library && slot.effect >= 0 && static_cast<size_t>(slot.effect) < snapshot.library->effects.size()) {
        node["effect"] = snapshot.library->effects[static_cast<size_t>(slot.effect)].name;
    } else {
        node["effect"] = nullptr;
    }
    node["enabled"] = slot.enabled;
    node["params"] = json::array();
    for (const float param : slot.params) {
        node["params"].push_back(num(param));
    }
    return node;
}

json layerJson(const Snapshot& snapshot, int index) {
    const LayerInfo& layer = snapshot.layers[static_cast<size_t>(index)];
    json node;
    node["index"] = index + 1;
    node["opacity"] = num(layer.opacity);
    node["bypass"] = layer.bypass;
    node["solo"] = layer.solo;
    node["speed"] = num(layer.speed);
    node["scale"] = num(layer.scale);
    if (layer.activeColumn >= 0) {
        node["activeColumn"] = layer.activeColumn + 1;
    } else {
        node["activeColumn"] = nullptr;
    }
    node["clips"] = json::array();
    for (int column = 0; column < kColumns; ++column) {
        json cell;
        cell["column"] = column + 1;
        cell["clip"] = nameOrNull(snapshot, layer.cells[static_cast<size_t>(column)]);
        node["clips"].push_back(cell);
    }
    node["effects"] = json::array();
    for (int slot = 0; slot < kSlots; ++slot) {
        node["effects"].push_back(slotJson(snapshot, layer.fx[static_cast<size_t>(slot)], slot + 1));
    }
    return node;
}

json deckListJson(const Snapshot& snapshot) {
    json list = json::array();
    for (int deck = 0; deck < kDecks; ++deck) {
        json node;
        node["index"] = deck + 1;
        node["name"] = snapshot.deckNames[static_cast<size_t>(deck)];
        node["used"] = snapshot.deckUsed[static_cast<size_t>(deck)];
        node["active"] = deck == snapshot.activeDeck;
        list.push_back(node);
    }
    return list;
}

Reply unavailable() {
    HttpResponse response = errorResponse(503, "starting", "The state is not available yet");
    response.setHeader("Retry-After", "1");
    return ready(std::move(response));
}

Reply notFound(const std::string& what = "This address does not exist") {
    return ready(errorResponse(404, "not_found", what));
}

Reply methodNotAllowed(const std::string& allow) {
    HttpResponse response = errorResponse(405, "method_not_allowed", "This method is not allowed here");
    response.setHeader("Allow", allow);
    return ready(std::move(response));
}

bool validPresetName(const std::string& name) {
    return !name.empty() && name.size() <= kMaxPresetNameBytes && sanitizePresetName(name) == name;
}

}

HttpResponse jsonResponse(int status, const std::string& body) {
    HttpResponse response;
    response.status = status;
    response.body = body;
    response.setHeader("Content-Type", "application/json; charset=utf-8");
    applySecurityHeaders(response);
    return response;
}

HttpResponse errorResponse(int status, const std::string& code, const std::string& message) {
    json node;
    node["error"]["code"] = code;
    node["error"]["message"] = message;
    return jsonResponse(status, node.dump());
}

void applySecurityHeaders(HttpResponse& response) {
    response.setHeader("Cache-Control", "no-store");
    response.setHeader("X-Content-Type-Options", "nosniff");
    response.setHeader("X-Frame-Options", "DENY");
    response.setHeader("Referrer-Policy", "no-referrer");
    response.setHeader("Cross-Origin-Resource-Policy", "same-origin");
    response.setHeader("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'");
}

PresetAccess defaultPresetAccess() {
    PresetAccess access;
    access.listUser = [] { return listUserPresets(); };
    access.readUser = [](const std::string& name, std::string& text, std::string& error) { return readUserPresetText(name, text, error); };
    return access;
}

Router::Router(Hub& hub, const RouterConfig& config, PresetAccess presets) : hub_(hub), config_(config), presets_(std::move(presets)), limiter_(config.limits) {}

HttpResponse Router::parseError(int status, const std::string& message) {
    ++stats_.badRequests;
    return errorResponse(status, "bad_request", message);
}

HttpResponse Router::timeoutResponse() {
    ++stats_.busy;
    HttpResponse response = errorResponse(503, "busy", "The program did not answer in time");
    response.setHeader("Retry-After", "1");
    return response;
}

Reply Router::handle(const HttpRequest& request, std::uint32_t peerAddress, double now) {
    ++stats_.requests;
    const std::string* host = request.header("host");
    if (host == nullptr || !hostAllowed(*host, config_.port, config_.allowRemote)) {
        ++stats_.rejected;
        return ready(errorResponse(403, "host_not_allowed", "Open the interface with the IP address or localhost"));
    }
    if (!originAllowed(request.header("origin"), request.header("sec-fetch-site"), *host)) {
        ++stats_.rejected;
        return ready(errorResponse(403, "origin_not_allowed", "Requests from other sites are not allowed"));
    }
    double retryAfter = 0.0;
    const RateVerdict verdict = limiter_.check(peerAddress, now, retryAfter);
    if (verdict != RateVerdict::Allow) {
        ++stats_.limited;
        HttpResponse response = errorResponse(429, verdict == RateVerdict::Locked ? "locked" : "rate_limited", verdict == RateVerdict::Locked ? "Too many wrong tokens, try again later" : "Too many requests, slow down");
        response.setHeader("Retry-After", std::to_string(static_cast<int>(std::ceil(std::max(retryAfter, 1.0)))));
        return ready(std::move(response));
    }
    if (request.method != "GET" && request.method != "POST" && request.method != "PUT") {
        return methodNotAllowed("GET, POST, PUT");
    }
    if (const PageAsset* asset = findPageAsset(request.path)) {
        if (request.method != "GET") {
            return methodNotAllowed("GET");
        }
        HttpResponse response;
        response.status = 200;
        response.body = asset->body;
        response.setHeader("Content-Type", asset->type);
        applySecurityHeaders(response);
        return ready(std::move(response));
    }
    if (request.path.compare(0, 5, "/api/") != 0 && request.path != "/api") {
        return notFound();
    }
    const std::string* authorization = request.header("authorization");
    std::string presented;
    if (authorization == nullptr || !parseBearer(*authorization, presented) || config_.token.empty() || !constantTimeEqual(presented, config_.token)) {
        ++stats_.authFailures;
        limiter_.recordAuthFailure(peerAddress, now);
        HttpResponse response = errorResponse(401, "unauthorized", "A valid token is required");
        response.setHeader("WWW-Authenticate", "Bearer");
        return ready(std::move(response));
    }
    limiter_.recordAuthSuccess(peerAddress);
    return dispatch(request);
}

Reply Router::dispatch(const HttpRequest& request) {
    const std::vector<std::string> segments = splitPath(request.path);
    if (segments.size() < 2 || segments[0] != "api" || segments[1] != "v1") {
        return notFound();
    }
    return route(request.method, std::vector<std::string>(segments.begin() + 2, segments.end()), request);
}

Reply Router::route(const std::string& method, const std::vector<std::string>& segments, const HttpRequest& request) {
    for (const std::string& part : segments) {
        if (part.empty()) {
            return notFound();
        }
    }
    if (segments.empty()) {
        if (method != "GET") {
            return methodNotAllowed("GET");
        }
        json node;
        node["name"] = "NyrexVJ";
        node["apiVersion"] = kApiVersion;
        node["endpoints"] = json::array({"status", "controls", "decks", "columns", "layers", "clips", "effects", "presets", "outputs"});
        return ready(jsonResponse(200, node.dump()));
    }
    const std::string& head = segments[0];
    const std::vector<std::string> rest(segments.begin() + 1, segments.end());
    if (head == "controls") {
        return controlRoute(method, rest, request);
    }
    if (head == "layers") {
        return layerRoute(method, rest, request);
    }
    if (head == "decks") {
        return deckRoute(method, rest);
    }
    if (head == "presets") {
        return presetRoute(method, rest, request);
    }
    if (head == "columns") {
        int column = 0;
        if (rest.size() != 2 || rest[1] != "trigger" || !parseIndex(rest[0], 1, kColumns, column)) {
            return notFound();
        }
        if (method != "POST") {
            return methodNotAllowed("POST");
        }
        return controlCommand("column/" + std::to_string(column), 1.0f);
    }

    const bool known = head == "status" || head == "clips" || head == "effects" || head == "outputs";
    if (!known || !rest.empty()) {
        return notFound();
    }
    if (method != "GET") {
        return methodNotAllowed("GET");
    }
    const std::shared_ptr<const Snapshot> snapshot = hub_.snapshot();
    if (!snapshot) {
        return unavailable();
    }
    json node;
    if (head == "status") {
        node["app"] = "NyrexVJ";
        node["version"] = snapshot->appVersion;
        node["apiVersion"] = kApiVersion;
        node["revision"] = snapshot->revision;
        node["composition"] = snapshot->composition;
        node["bpm"] = num(snapshot->bpm);
        node["beatsPerBar"] = snapshot->beatsPerBar;
        node["activeDeck"] = snapshot->activeDeck + 1;
        node["blackout"] = snapshot->blackout;
        node["masterLevel"] = num(snapshot->masterLevel);
        node["fps"] = num(snapshot->fps);
        node["output"]["width"] = snapshot->width;
        node["output"]["height"] = snapshot->height;
        node["audioActive"] = snapshot->audioActive;
        node["layers"] = kLayers;
        node["columns"] = kColumns;
        node["decks"] = kDecks;
    } else if (head == "clips") {
        node["clips"] = json::array();
        if (snapshot->library) {
            int number = 0;
            for (const ClipInfo& clip : snapshot->library->clips) {
                json item;
                item["index"] = ++number;
                item["name"] = clip.name;
                item["kind"] = clip.kind;
                node["clips"].push_back(item);
            }
        }
    } else if (head == "effects") {
        node["effects"] = json::array();
        if (snapshot->library) {
            int number = 0;
            for (const EffectInfo& effect : snapshot->library->effects) {
                json item;
                item["index"] = ++number;
                item["name"] = effect.name;
                item["params"] = effect.params;
                item["graph"] = effect.graph;
                node["effects"].push_back(item);
            }
        }
    } else {
        node["outputs"] = json::array();
        int number = 0;
        for (const OutputInfo& output : snapshot->outputs) {
            json item;
            item["index"] = ++number;
            item["name"] = output.name;
            item["enabled"] = output.enabled;
            item["open"] = output.open;
            item["fullscreen"] = output.fullscreen;
            item["monitor"] = output.monitor + 1;
            item["ndi"] = output.ndi;
            if (output.ndi) {
                item["ndiName"] = output.ndiName;
            }
            node["outputs"].push_back(item);
        }
    }
    return ready(jsonResponse(200, node.dump()));
}

Reply Router::controlRoute(const std::string& method, const std::vector<std::string>& rest, const HttpRequest& request) {
    const auto& targets = controlTargets();
    if (rest.empty()) {
        if (method != "GET") {
            return methodNotAllowed("GET");
        }
        const std::shared_ptr<const Snapshot> snapshot = hub_.snapshot();
        if (!snapshot) {
            return unavailable();
        }
        std::string prefix;
        const std::string& query = request.query;
        if (query.compare(0, 7, "prefix=") == 0) {
            if (!decodePercent(query.substr(7), prefix) || prefix.size() > 64) {
                return ready(errorResponse(400, "bad_request", "The prefix is not valid"));
            }
        } else if (!query.empty()) {
            return ready(errorResponse(400, "bad_request", "Only the prefix parameter is supported"));
        }
        json node;
        node["controls"] = json::array();
        for (size_t i = 0; i < targets.size(); ++i) {
            if (!prefix.empty() && targets[i].id.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            json item;
            item["id"] = targets[i].id;
            item["kind"] = kindName(targets[i].kind);
            item["value"] = num(valueAt(*snapshot, i));
            node["controls"].push_back(item);
        }
        return ready(jsonResponse(200, node.dump()));
    }
    const std::string id = joinPath(rest, 0);
    const int index = controlTargetIndex(id);
    if (index < 0) {
        return notFound("This control does not exist");
    }
    if (method == "GET") {
        const std::shared_ptr<const Snapshot> snapshot = hub_.snapshot();
        if (!snapshot) {
            return unavailable();
        }
        json node;
        node["id"] = id;
        node["kind"] = kindName(targets[static_cast<size_t>(index)].kind);
        node["value"] = num(valueAt(*snapshot, static_cast<size_t>(index)));
        return ready(jsonResponse(200, node.dump()));
    }
    if (method != "PUT" && method != "POST") {
        return methodNotAllowed("GET, PUT, POST");
    }
    json body;
    HttpResponse error;
    if (!readBody(request, body, error)) {
        return ready(std::move(error));
    }
    const ControlKind kind = targets[static_cast<size_t>(index)].kind;
    float value = 1.0f;
    bool given = false;
    if (body.contains("value")) {
        const json& raw = body["value"];
        if (raw.is_boolean()) {
            value = raw.get<bool>() ? 1.0f : 0.0f;
        } else if (raw.is_number()) {
            const double number = raw.get<double>();
            if (!std::isfinite(number) || number < 0.0 || number > 1.0) {
                return ready(errorResponse(400, "value_out_of_range", "The value must be between 0 and 1"));
            }
            value = static_cast<float>(number);
        } else {
            return ready(errorResponse(400, "invalid_value", "The value must be a number or true or false"));
        }
        given = true;
    }
    if (!given && kind != ControlKind::Trigger) {
        return ready(errorResponse(400, "missing_value", "This control needs a value"));
    }
    return controlCommand(id, value);
}

Reply Router::controlCommand(const std::string& id, float value) {
    const int index = controlTargetIndex(id);
    if (index < 0) {
        return notFound("This control does not exist");
    }
    Command command;
    command.type = CommandType::Control;
    command.control = index;
    command.value = value;
    const std::string kind = kindName(controlTargets()[static_cast<size_t>(index)].kind);
    return submit(command, [id, kind](const Result& result, HttpResponse& response) {
        if (!result.ok) {
            response = errorResponse(result.status, result.code, result.message);
            return;
        }
        json node;
        node["id"] = id;
        node["kind"] = kind;
        node["value"] = num(result.value);
        response = jsonResponse(200, node.dump());
    });
}

Reply Router::layerRoute(const std::string& method, const std::vector<std::string>& rest, const HttpRequest&) {
    if (rest.empty()) {
        if (method != "GET") {
            return methodNotAllowed("GET");
        }
        const std::shared_ptr<const Snapshot> snapshot = hub_.snapshot();
        if (!snapshot) {
            return unavailable();
        }
        json node;
        node["layers"] = json::array();
        for (int layer = 0; layer < kLayers; ++layer) {
            node["layers"].push_back(layerJson(*snapshot, layer));
        }
        return ready(jsonResponse(200, node.dump()));
    }
    int layer = 0;
    if (!parseIndex(rest[0], 1, kLayers, layer)) {
        return notFound("This layer does not exist");
    }
    if (rest.size() == 1) {
        if (method != "GET") {
            return methodNotAllowed("GET");
        }
        const std::shared_ptr<const Snapshot> snapshot = hub_.snapshot();
        if (!snapshot) {
            return unavailable();
        }
        return ready(jsonResponse(200, layerJson(*snapshot, layer - 1).dump()));
    }
    if (rest.size() == 2 && rest[1] == "clear") {
        if (method != "POST") {
            return methodNotAllowed("POST");
        }
        return controlCommand("layer/" + std::to_string(layer) + "/clear", 1.0f);
    }
    int column = 0;
    if (rest.size() == 4 && rest[1] == "clips" && rest[3] == "trigger" && parseIndex(rest[2], 1, kColumns, column)) {
        if (method != "POST") {
            return methodNotAllowed("POST");
        }
        const std::shared_ptr<const Snapshot> snapshot = hub_.snapshot();
        if (!snapshot) {
            return unavailable();
        }
        if (snapshot->layers[static_cast<size_t>(layer - 1)].cells[static_cast<size_t>(column - 1)] < 0) {
            return ready(errorResponse(409, "cell_empty", "This cell has no clip"));
        }
        return controlCommand("layer/" + std::to_string(layer) + "/clip/" + std::to_string(column), 1.0f);
    }
    return notFound();
}

Reply Router::deckRoute(const std::string& method, const std::vector<std::string>& rest) {
    if (rest.empty()) {
        if (method != "GET") {
            return methodNotAllowed("GET");
        }
        const std::shared_ptr<const Snapshot> snapshot = hub_.snapshot();
        if (!snapshot) {
            return unavailable();
        }
        json node;
        node["decks"] = deckListJson(*snapshot);
        return ready(jsonResponse(200, node.dump()));
    }
    if (rest.size() == 1 && (rest[0] == "next" || rest[0] == "previous")) {
        if (method != "POST") {
            return methodNotAllowed("POST");
        }
        return controlCommand("deck/" + rest[0], 1.0f);
    }
    int deck = 0;
    if (rest.size() == 2 && rest[1] == "activate" && parseIndex(rest[0], 1, kDecks, deck)) {
        if (method != "POST") {
            return methodNotAllowed("POST");
        }
        const std::shared_ptr<const Snapshot> snapshot = hub_.snapshot();
        if (!snapshot) {
            return unavailable();
        }
        if (!snapshot->deckUsed[static_cast<size_t>(deck - 1)]) {
            return ready(errorResponse(409, "deck_not_used", "This deck is empty"));
        }
        return controlCommand("deck/" + std::to_string(deck), 1.0f);
    }
    return notFound();
}

Reply Router::presetRoute(const std::string& method, const std::vector<std::string>& rest, const HttpRequest& request) {
    if (rest.empty()) {
        if (method != "GET") {
            return methodNotAllowed("GET");
        }
        const std::shared_ptr<const Snapshot> snapshot = hub_.snapshot();
        if (!snapshot) {
            return unavailable();
        }
        json node;
        node["factory"] = json::array();
        if (snapshot->library) {
            for (const std::string& name : snapshot->library->factoryPresets) {
                node["factory"].push_back(name);
            }
        }
        node["user"] = json::array();
        if (presets_.listUser) {
            for (const std::string& name : presets_.listUser()) {
                node["user"].push_back(name);
            }
        }
        return ready(jsonResponse(200, node.dump()));
    }
    if (rest.size() != 1 || rest[0] != "load") {
        return notFound();
    }
    if (method != "POST") {
        return methodNotAllowed("POST");
    }
    json body;
    HttpResponse error;
    if (!readBody(request, body, error)) {
        return ready(std::move(error));
    }
    if (!body.contains("source") || !body["source"].is_string() || !body.contains("name") || !body["name"].is_string()) {
        return ready(errorResponse(400, "bad_request", "Send the source and the name of the preset"));
    }
    const std::string source = body["source"].get<std::string>();
    const std::string name = body["name"].get<std::string>();
    if (!validPresetName(name)) {
        return ready(errorResponse(400, "invalid_name", "The preset name is not valid"));
    }
    Command command;
    command.name = name;
    if (source == "factory") {
        const std::shared_ptr<const Snapshot> snapshot = hub_.snapshot();
        if (!snapshot) {
            return unavailable();
        }
        const bool known = snapshot->library && std::find(snapshot->library->factoryPresets.begin(), snapshot->library->factoryPresets.end(), name) != snapshot->library->factoryPresets.end();
        if (!known) {
            return notFound("This preset does not exist");
        }
        command.type = CommandType::LoadFactoryPreset;
    } else if (source == "user") {
        const std::vector<std::string> names = presets_.listUser ? presets_.listUser() : std::vector<std::string>();
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            return notFound("This preset does not exist");
        }
        std::string text;
        std::string readError;
        if (!presets_.readUser || !presets_.readUser(name, text, readError)) {
            return ready(errorResponse(500, "read_failed", "The preset file could not be read"));
        }
        command.type = CommandType::LoadUserPreset;
        command.text = std::move(text);
    } else {
        return ready(errorResponse(400, "invalid_source", "The source must be factory or user"));
    }
    return submit(command, [name, source](const Result& result, HttpResponse& response) {
        if (!result.ok) {
            response = errorResponse(result.status, result.code, result.message);
            return;
        }
        json node;
        node["loaded"] = name;
        node["source"] = source;
        response = jsonResponse(200, node.dump());
    });
}

Reply Router::submit(const Command& command, std::function<void(const Result&, HttpResponse&)> finish) {
    std::shared_ptr<Ticket> ticket = hub_.submit(command);
    if (!ticket) {
        ++stats_.busy;
        HttpResponse response = errorResponse(503, "busy", "Too many commands are waiting");
        response.setHeader("Retry-After", "1");
        return ready(std::move(response));
    }
    ++stats_.commands;
    Reply reply;
    reply.ticket = std::move(ticket);
    reply.finish = std::move(finish);
    return reply;
}

}
