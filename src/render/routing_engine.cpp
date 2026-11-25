#include "render/engine.hpp"

#include "core/log.hpp"

#include <algorithm>

namespace nvj {

namespace {

void clearTransparent(const RenderTarget& target) {
    target.bind();
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

bool createLike(RenderTarget& target, const RenderTarget& like) {
    return like.bytesPerPixel == 8 ? target.createFloat(like.width, like.height) : target.create(like.width, like.height);
}

constexpr unsigned long long kPreviewFresh = 4;

}

void Engine::blitFiltered(const RenderTarget& source, const RenderTarget& target, bool linear) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.fbo);
    glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, target.width, target.height, GL_COLOR_BUFFER_BIT, linear ? GL_LINEAR : GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

routing::Plan Engine::planRouting(const std::array<int, kLayerCount>& clipOf, int deck) {
    routing::RenderedLayers rendered{};
    std::vector<routing::Consumer> consumers;
    const std::vector<ClipDef>& library = clipLibrary();
    const std::vector<routing::Source>& sources = routing::clipSources();
    for (int l = 0; l < kLayerCount; ++l) {
        const int clip = clipOf[static_cast<size_t>(l)];
        rendered[static_cast<size_t>(l)] = clip >= 0;
        if (clip < 0 || clip >= static_cast<int>(library.size()) || !library[static_cast<size_t>(clip)].isRoute()) {
            continue;
        }
        const int index = library[static_cast<size_t>(clip)].sceneIndex;
        if (index >= 0 && index < static_cast<int>(sources.size())) {
            consumers.push_back(routing::Consumer{routing::ConsumerKind::Layer, l, sources[static_cast<size_t>(index)]});
        }
    }
    if (deck == 0) {
        for (int k = 0; k < routing::kSenderCount; ++k) {
            const routing::Source& source = senderSources_[static_cast<size_t>(k)];
            if (source.kind != routing::SourceKind::Master) {
                consumers.push_back(routing::Consumer{routing::ConsumerKind::Sender, k, source});
            }
        }
    }
    routing::Plan plan = routing::makePlan(consumers, rendered);
    if (deck == 0) {
        routePlan_ = plan;
    }
    return plan;
}

void Engine::routeCapture(int deck, int slot, const RenderTarget& source, const routing::Plan& plan) {
    if (slot < 0 || slot >= routing::kSlotCount || deck < 0 || deck >= routing::kDeckPasses || !source.fbo) {
        return;
    }
    const size_t s = static_cast<size_t>(slot);
    if (deck == 0 && routePreviewsWanted_) {
        RenderTarget& preview = routePreviews_[s];
        if (!preview.fbo) {
            preview.create(kRoutePreviewWidth, kRoutePreviewHeight);
        }
        if (preview.fbo && (frameId_ % 2 == 0 || routePreviewFrame_[s] == 0)) {
            downsample(source, preview);
            routePreviewFrame_[s] = frameId_;
        }
    }
    if (!plan.capture[s]) {
        return;
    }
    RouteCapture& capture = routeCaptures_[static_cast<size_t>(deck)][s];
    const bool keep = plan.keepPrevious[s];
    int write = capture.current;
    if (keep && capture.target[static_cast<size_t>(capture.current)].fbo && capture.frame[static_cast<size_t>(capture.current)] != 0) {
        write = 1 - capture.current;
    }
    RenderTarget& target = capture.target[static_cast<size_t>(write)];
    if (!target.fbo || routing::needsResize(target.width, target.height, source.width, source.height) || target.bytesPerPixel != source.bytesPerPixel) {
        const size_t had = target.bytes();
        const size_t wanted = static_cast<size_t>(routing::captureBytes(source.width, source.height, source.bytesPerPixel, 1));
        if (routeBytes() - had + wanted > kRouteBudgetBytes) {
            if (!routeBudgetLogged_) {
                routeBudgetLogged_ = true;
                logLine("Video routing: the memory for routed pictures (512 MiB) is used up, a source is not captured");
            }
            return;
        }
        target.destroy();
        if (!createLike(target, source)) {
            target.destroy();
            return;
        }
    }
    const routing::Compat compat = routing::checkSurfaces(
        routing::Surface{source.width, source.height, source.bytesPerPixel, routing::Alpha::Premultiplied},
        routing::Surface{target.width, target.height, target.bytesPerPixel, routing::Alpha::Premultiplied});
    if (!compat.ok) {
        return;
    }
    blitFiltered(source, target, compat.linear);
    capture.frame[static_cast<size_t>(write)] = frameId_;
    capture.current = write;
    if (!keep) {
        RenderTarget& other = capture.target[static_cast<size_t>(1 - write)];
        if (other.fbo) {
            other.destroy();
            capture.frame[static_cast<size_t>(1 - write)] = 0;
        }
    }
    routeCache_.use(deck * routing::kSlotCount + slot, frameId_);
    ++routeCaptureCount_;
}

void Engine::drawRouted(int sourceIndex, int instance, const RenderTarget& target) {
    const std::vector<routing::Source>& sources = routing::clipSources();
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(sources.size())) {
        clearTransparent(target);
        return;
    }
    const routing::Source& source = sources[static_cast<size_t>(sourceIndex)];
    const int deck = instance >= kLayerCount && instance < kLayerCount * 2 ? 1 : 0;
    const RouteCapture& capture = routeCaptures_[static_cast<size_t>(deck)][static_cast<size_t>(routing::slotOf(source))];
    const unsigned long long wanted = source.previousFrame ? frameId_ - 1 : frameId_;
    int use = -1;
    for (int i = 0; i < 2; ++i) {
        const RenderTarget& candidate = capture.target[static_cast<size_t>(i)];
        if (candidate.fbo && capture.frame[static_cast<size_t>(i)] == wanted) {
            use = i;
        }
    }
    if (use < 0) {
        clearTransparent(target);
        return;
    }
    const RenderTarget& from = capture.target[static_cast<size_t>(use)];
    const routing::Compat compat = routing::checkSurfaces(
        routing::Surface{from.width, from.height, from.bytesPerPixel, routing::Alpha::Premultiplied},
        routing::Surface{target.width, target.height, target.bytesPerPixel, routing::Alpha::Premultiplied});
    if (!compat.ok) {
        clearTransparent(target);
        return;
    }
    blitFiltered(from, target, compat.linear);
}

void Engine::destroyRouteSlot(int deck, int slot) {
    RouteCapture& capture = routeCaptures_[static_cast<size_t>(deck)][static_cast<size_t>(slot)];
    for (size_t i = 0; i < 2; ++i) {
        capture.target[i].destroy();
        capture.frame[i] = 0;
    }
    capture.current = 0;
}

void Engine::serviceRouting() {
    for (int k = 0; k < routing::kSenderCount; ++k) {
        const size_t index = static_cast<size_t>(k);
        const routing::Source& source = senderSources_[index];
        RenderTarget& target = senderTargets_[index];
        senderLive_[index] = false;
        if (source.kind == routing::SourceKind::Master || !routing::validSource(source) || !out_.fbo) {
            if (target.fbo) {
                target.destroy();
            }
            continue;
        }
        if (!target.fbo || routing::needsResize(target.width, target.height, out_.width, out_.height)) {
            target.destroy();
            if (!target.create(out_.width, out_.height)) {
                target.destroy();
                continue;
            }
        }
        const RouteCapture& capture = routeCaptures_[0][static_cast<size_t>(routing::slotOf(source))];
        int use = -1;
        for (int i = 0; i < 2; ++i) {
            if (capture.target[static_cast<size_t>(i)].fbo && capture.frame[static_cast<size_t>(i)] == frameId_) {
                use = i;
            }
        }
        if (use >= 0) {
            finalPass(capture.target[static_cast<size_t>(use)], ColorGrade{}, 1.0f, target);
            senderLive_[index] = true;
        } else {
            target.bind();
            glDisable(GL_BLEND);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    for (const int key : routeCache_.expired(frameId_)) {
        destroyRouteSlot(key / routing::kSlotCount, key % routing::kSlotCount);
        routeCache_.drop(key);
    }
    if (!routePreviewsWanted_) {
        for (size_t s = 0; s < routePreviews_.size(); ++s) {
            if (routePreviews_[s].fbo) {
                routePreviews_[s].destroy();
                routePreviewFrame_[s] = 0;
            }
        }
    }
}

void Engine::releaseRouting() {
    for (int deck = 0; deck < routing::kDeckPasses; ++deck) {
        for (int slot = 0; slot < routing::kSlotCount; ++slot) {
            destroyRouteSlot(deck, slot);
        }
    }
    for (RenderTarget& target : senderTargets_) {
        target.destroy();
    }
    for (RenderTarget& target : routePreviews_) {
        target.destroy();
    }
    routePreviewFrame_.fill(0);
    senderLive_.fill(false);
    routeCache_.clear();
    routeBudgetLogged_ = false;
}

void Engine::setSenderSource(routing::SenderKind kind, const routing::Source& source) {
    const size_t index = static_cast<size_t>(kind);
    if (index >= senderSources_.size()) {
        return;
    }
    routing::Source chosen = source;
    if (!routing::validSource(chosen) || chosen.previousFrame) {
        chosen = routing::Source{};
    }
    senderSources_[index] = chosen;
}

routing::Source Engine::senderSource(routing::SenderKind kind) const {
    const size_t index = static_cast<size_t>(kind);
    return index < senderSources_.size() ? senderSources_[index] : routing::Source{};
}

GLuint Engine::routedTexture(routing::SenderKind kind) const {
    const size_t index = static_cast<size_t>(kind);
    if (index >= senderSources_.size()) {
        return 0;
    }
    if (senderSources_[index].kind == routing::SourceKind::Master) {
        return out_.texture;
    }
    return senderTargets_[index].fbo ? senderTargets_[index].texture : 0;
}

bool Engine::senderSourceLive(routing::SenderKind kind) const {
    const size_t index = static_cast<size_t>(kind);
    if (index >= senderSources_.size()) {
        return false;
    }
    return senderSources_[index].kind == routing::SourceKind::Master || senderLive_[index];
}

RouteSlotStatus Engine::routeSlot(int slot) const {
    RouteSlotStatus status;
    if (slot < 0 || slot >= routing::kSlotCount) {
        return status;
    }
    const size_t s = static_cast<size_t>(slot);
    unsigned long long newest = 0;
    for (size_t deck = 0; deck < static_cast<size_t>(routing::kDeckPasses); ++deck) {
        const RouteCapture& capture = routeCaptures_[deck][s];
        for (size_t i = 0; i < 2; ++i) {
            if (capture.target[i].fbo) {
                status.captured = true;
                status.bytes += capture.target[i].bytes();
                if (deck == 0) {
                    ++status.copies;
                    status.width = capture.target[i].width;
                    status.height = capture.target[i].height;
                    newest = std::max(newest, capture.frame[i]);
                }
            }
        }
    }
    status.references = routePlan_.references[s];
    if (routePreviews_[s].fbo) {
        status.preview = routePreviews_[s].texture;
        status.bytes += routePreviews_[s].bytes();
        newest = std::max(newest, routePreviewFrame_[s]);
    }
    status.fresh = newest != 0 && frameId_ >= newest && frameId_ - newest <= kPreviewFresh;
    return status;
}

size_t Engine::routeBytes() const {
    size_t total = 0;
    for (const auto& deck : routeCaptures_) {
        for (const RouteCapture& capture : deck) {
            total += capture.target[0].bytes() + capture.target[1].bytes();
        }
    }
    for (const RenderTarget& target : senderTargets_) {
        total += target.bytes();
    }
    for (const RenderTarget& target : routePreviews_) {
        total += target.bytes();
    }
    return total;
}

int Engine::routeCapturesAlive() const {
    int count = 0;
    for (const auto& deck : routeCaptures_) {
        for (const RouteCapture& capture : deck) {
            if (capture.target[0].fbo || capture.target[1].fbo) {
                ++count;
            }
        }
    }
    return count;
}

}
