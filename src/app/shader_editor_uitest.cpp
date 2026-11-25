#include "app/app.hpp"

#include "app/shader_editor_dialog.hpp"
#include "core/log.hpp"
#include "core/shader_editor.hpp"
#include "render/clip_library.hpp"

#include <glad/gl.h>

#include <imgui.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace nvj {

namespace {

namespace fs = std::filesystem;
namespace se = shaderedit;

constexpr int kMaxFrames = 9000;

const char* kRedShader = "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0, 0.0, 0.0, 1.0); }";
const char* kGreenShader = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.0, 1.0, 0.0, 1.0); }";

struct Pixel {
    int r = -1;
    int g = -1;
    int b = -1;
};

Pixel previewCentre(Engine& engine) {
    Pixel result;
    const GLuint texture = engine.shaderPlaygroundTexture();
    if (!texture) {
        return result;
    }
    const int width = engine.shaderPlaygroundWidth();
    const int height = engine.shaderPlaygroundHeight();
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    unsigned char rgba[4] = {};
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        result.r = rgba[0];
        result.g = rgba[1];
        result.b = rgba[2];
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return result;
}

bool nearColour(const Pixel& pixel, int r, int g, int b) {
    return std::abs(pixel.r - r) <= 3 && std::abs(pixel.g - g) <= 3 && std::abs(pixel.b - b) <= 3;
}

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string clipText(const std::string& text, size_t length = 160) {
    std::string flat;
    for (const char c : text) {
        flat += (c == '\n' || c == '\r') ? ' ' : c;
    }
    return flat.size() > length ? flat.substr(0, length) : flat;
}

}

void App::startShaderEditorTest() {
    shaderEditorTest_ = ShaderEditorUiTest{};
    shaderEditorTest_.active = true;
}

std::string App::shaderEditorTestReport() const {
    nlohmann::json report;
    nlohmann::json checks = nlohmann::json::array();
    for (const ShaderEditorUiTest::Item& item : shaderEditorTest_.items) {
        nlohmann::json entry;
        entry["name"] = item.name;
        entry["passed"] = item.passed;
        entry["detail"] = item.detail;
        checks.push_back(entry);
    }
    report["checks"] = checks;
    report["failed"] = shaderEditorTest_.failed;
    report["passed"] = shaderEditorTest_.failed == 0 && !shaderEditorTest_.items.empty();
    report["frames"] = shaderEditorTest_.totalFrames;
    return report.dump(2);
}

int App::tickShaderEditorTest() {
    ShaderEditorUiTest& t = shaderEditorTest_;
    if (!t.active) {
        return 0;
    }
    if (t.steps.empty()) {
        buildShaderEditorTest();
    }
    ++t.totalFrames;
    if (t.totalFrames > kMaxFrames) {
        t.items.push_back({"the script finishes in time", "stopped at step " + std::to_string(t.index), false});
        ++t.failed;
        t.active = false;
        return 2;
    }
    if (t.index >= t.steps.size()) {
        t.active = false;
        return t.failed == 0 ? 1 : 2;
    }
    ShaderEditorUiTest::Step& step = t.steps[t.index];
    if (!t.started) {
        t.started = true;
        t.waited = 0;
        if (step.act) {
            step.act();
        }
        return 0;
    }
    ++t.waited;
    const bool done = step.until ? (step.until() || t.waited >= step.frames) : t.waited >= step.frames;
    if (done) {
        if (step.check) {
            step.check();
        }
        ++t.index;
        t.started = false;
    }
    return 0;
}

void App::buildShaderEditorTest() {
    ShaderEditorUiTest& t = shaderEditorTest_;
    ShaderEditorDialog& d = shaderEditor_;
    using Step = ShaderEditorUiTest::Step;

    const auto record = [this](const std::string& name, bool passed, const std::string& detail) {
        shaderEditorTest_.items.push_back({name, detail, passed});
        shaderEditorTest_.failed += passed ? 0 : 1;
        logLine("Shader editor UI test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    };
    const auto add = [&t](std::function<void()> act, int frames, std::function<bool()> until = nullptr, std::function<void()> check = nullptr) {
        Step step;
        step.act = std::move(act);
        step.frames = frames;
        step.until = std::move(until);
        step.check = std::move(check);
        t.steps.push_back(std::move(step));
    };
    const auto press = [&add](ImGuiKey key, bool ctrl, bool shift) {
        add([key, ctrl, shift]() {
            ImGuiIO& io = ImGui::GetIO();
            if (ctrl) {
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
            }
            if (shift) {
                io.AddKeyEvent(ImGuiMod_Shift, true);
            }
            io.AddKeyEvent(key, true);
            io.AddKeyEvent(key, false);
            if (shift) {
                io.AddKeyEvent(ImGuiMod_Shift, false);
            }
            if (ctrl) {
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
            }
        }, 4);
    };
    const auto type = [&add](const std::string& text) {
        add([text]() {
            ImGuiIO& io = ImGui::GetIO();
            for (const char c : text) {
                io.AddInputCharacter(static_cast<unsigned int>(static_cast<unsigned char>(c)));
            }
        }, 3);
    };
    const auto click = [&add, &d](int line, float xOffset) {
        add([&d, line, xOffset]() {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(d.testTextLeft + xOffset, d.testOriginY + (static_cast<float>(line) + 0.5f) * d.testLineHeight);
        }, 3);
        add([]() { ImGui::GetIO().AddMouseButtonEvent(0, true); }, 1);
        add([]() { ImGui::GetIO().AddMouseButtonEvent(0, false); }, 3);
    };
    const auto compiled = [this, &d]() {
        const ShaderPlaygroundStatus& s = engine_.shaderPlayground();
        return d.engineRevision != 0 && s.workingRevision == d.engineRevision;
    };
    const auto attempted = [this, &d]() {
        const ShaderPlaygroundStatus& s = engine_.shaderPlayground();
        return d.engineRevision != 0 && s.attemptedRevision == d.engineRevision && !s.compiling && !d.scheduler.inFlight();
    };

    add(nullptr, 600, compiled, [this, record, compiled]() {
        const ShaderPlaygroundStatus& s = engine_.shaderPlayground();
        record("the editor opens with a template that compiles", compiled() && s.error.empty(), "revision " + std::to_string(s.workingRevision));
    });

    click(2, 40.0f);
    add(nullptr, 3, nullptr, [&d, record]() {
        const se::Position position = d.model.positionOf(d.model.cursor());
        record("a mouse click puts the cursor on that line", d.editorActive && position.line == 2, "line " + std::to_string(position.line + 1) + ", column " + std::to_string(position.column + 1));
    });

    press(ImGuiKey_A, true, false);
    press(ImGuiKey_Delete, false, false);
    add(nullptr, 2, nullptr, [&d, record]() {
        record("Ctrl+A and Delete clear the text", d.model.size() == 0, std::to_string(d.model.size()) + " bytes left");
    });
    add(nullptr, 900, attempted, [this, &d, record]() {
        const ShaderPlaygroundStatus& s = engine_.shaderPlayground();
        record("an empty text is reported and the last working version stays", !s.error.empty() && s.hasWorking && s.workingRevision != d.engineRevision, clipText(s.error));
    });

    type(kRedShader);
    add(nullptr, 900, compiled, [this, &d, record]() {
        record("typed text is what the editor holds", d.model.text() == kRedShader, std::to_string(d.model.size()) + " bytes");
        const Pixel pixel = previewCentre(engine_);
        record("the typed shader compiles and shows red", nearColour(pixel, 255, 0, 0), "got (" + std::to_string(pixel.r) + "," + std::to_string(pixel.g) + "," + std::to_string(pixel.b) + ")");
    });

    press(ImGuiKey_End, true, false);
    press(ImGuiKey_Enter, false, false);
    type("oops oops;");
    add(nullptr, 900, [this, &d]() {
        const ShaderPlaygroundStatus& s = engine_.shaderPlayground();
        return d.engineRevision != 0 && s.attemptedRevision == d.engineRevision && !s.compiling && !d.scheduler.inFlight() && s.workingRevision != d.engineRevision;
    }, [this, &d, record]() {
        const ShaderPlaygroundStatus& s = engine_.shaderPlayground();
        const int line = se::firstErrorLine(d.diagnostics);
        record("the mistake is shown on its line", line == 2 && !d.diagnostics.empty(), "line " + std::to_string(line) + ", " + clipText(s.error));
        const Pixel pixel = previewCentre(engine_);
        record("the preview keeps showing the last working version", s.hasWorking && nearColour(pixel, 255, 0, 0), "got (" + std::to_string(pixel.r) + "," + std::to_string(pixel.g) + "," + std::to_string(pixel.b) + ")");
    });

    press(ImGuiKey_Z, true, false);
    press(ImGuiKey_Z, true, false);
    add(nullptr, 900, compiled, [&d, this, record]() {
        record("two undo steps remove the newline and the typed mistake", d.model.text() == kRedShader, clipText(d.model.text()));
        record("the problem list is empty again", d.diagnostics.empty() && engine_.shaderPlayground().error.empty(), std::to_string(d.diagnostics.size()) + " entries");
    });

    press(ImGuiKey_Y, true, false);
    add(nullptr, 2, nullptr, [&d, record]() {
        record("Ctrl+Y redoes the last undo", d.model.text() != kRedShader, clipText(d.model.text()));
    });
    press(ImGuiKey_Z, true, false);
    add(nullptr, 2, nullptr, [&d, record]() {
        record("and Ctrl+Z takes it back", d.model.text() == kRedShader, clipText(d.model.text()));
    });

    press(ImGuiKey_Home, false, false);
    press(ImGuiKey_Slash, true, false);
    add(nullptr, 2, nullptr, [&d, record]() {
        record("Ctrl+/ comments the line", d.model.text().rfind("//", 0) == 0, clipText(d.model.text(), 60));
    });
    press(ImGuiKey_Slash, true, false);
    add(nullptr, 2, nullptr, [&d, record]() {
        record("and uncomments it again", d.model.text() == kRedShader, clipText(d.model.text(), 60));
    });

    press(ImGuiKey_RightArrow, false, true);
    press(ImGuiKey_RightArrow, false, true);
    add(nullptr, 2, nullptr, [&d, record]() {
        record("Shift+Right selects characters", d.model.hasSelection() && d.model.selectedText().size() == 2, d.model.selectedText());
    });
    type("X");
    add(nullptr, 2, nullptr, [&d, record]() {
        record("typing replaces the selection", d.model.text().rfind("X", 0) == 0 && d.model.size() == std::string(kRedShader).size() - 1, clipText(d.model.text(), 60));
    });
    press(ImGuiKey_Z, true, false);
    add(nullptr, 2, nullptr, [&d, record]() {
        record("undo restores the replaced text", d.model.text() == kRedShader, clipText(d.model.text(), 60));
    });

    add(nullptr, 900, compiled);
    press(ImGuiKey_S, true, false);
    add(nullptr, 60, [&d]() { return d.modal == ShaderEditorDialog::Modal::SaveAs; }, [&d, record]() {
        record("Ctrl+S on a new shader asks for a name", d.modal == ShaderEditorDialog::Modal::SaveAs, "modal state " + std::to_string(static_cast<int>(d.modal)));
    });
    type("uitest_saved");
    press(ImGuiKey_Enter, false, false);
    add(nullptr, 120, [&d]() { return !d.fileName.empty(); }, [&d, record]() {
        const fs::path file = fs::path(shaderDirectory()) / "uitest_saved.glsl";
        record("Enter in the name field saves the shader asset", d.fileName == "uitest_saved.glsl" && fs::exists(file) && readText(file) == kRedShader, d.fileName);
        record("the saved shader is not marked as changed", !d.model.dirty(), d.model.dirty() ? "still dirty" : "clean");
    });

    add([&d]() {
        const fs::path file = fs::path(shaderDirectory()) / "uitest_saved.glsl";
        writeText(file, kGreenShader);
        std::error_code ec;
        fs::last_write_time(file, fs::file_time_type::clock::now() + std::chrono::seconds(300), ec);
    }, 1200, [&d]() { return d.model.text() == kGreenShader; }, [&d, record]() {
        record("a change from another program is loaded when nothing is unsaved", d.model.text() == kGreenShader && !d.model.dirty(), clipText(d.model.text(), 60));
    });
    add(nullptr, 900, compiled, [this, record]() {
        const Pixel pixel = previewCentre(engine_);
        record("and the preview shows the loaded version", nearColour(pixel, 0, 255, 0), "got (" + std::to_string(pixel.r) + "," + std::to_string(pixel.g) + "," + std::to_string(pixel.b) + ")");
    });

    click(0, 20.0f);
    type("// mine\n");
    add(nullptr, 2, nullptr, [&d, record]() {
        record("typing marks the shader as changed", d.model.dirty(), d.model.dirty() ? "dirty" : "clean");
    });
    add([]() {
        const fs::path file = fs::path(shaderDirectory()) / "uitest_saved.glsl";
        writeText(file, std::string("// other program\n") + kRedShader);
        std::error_code ec;
        fs::last_write_time(file, fs::file_time_type::clock::now() + std::chrono::seconds(600), ec);
    }, 2);
    press(ImGuiKey_S, true, false);
    add(nullptr, 60, [&d]() { return d.modal == ShaderEditorDialog::Modal::Conflict; }, [&d, record]() {
        record("saving over a change from another program shows the conflict dialog", d.modal == ShaderEditorDialog::Modal::Conflict, "modal state " + std::to_string(static_cast<int>(d.modal)));
        const std::string onDisk = readText(fs::path(shaderDirectory()) / "uitest_saved.glsl");
        record("nothing was written before the decision", onDisk.rfind("// other program", 0) == 0, clipText(onDisk, 60));
    });
    add([]() {
        std::error_code ec;
        fs::remove(fs::path(shaderDirectory()) / "uitest_saved.glsl", ec);
    }, 2);
}

}
