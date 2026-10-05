// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/assets.hpp"
#include "rich4/graphics.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <charconv>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
std::size_t number(const char* value) {
    const std::string text(value);
    std::size_t parsed{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) throw std::invalid_argument("invalid resource index");
    return parsed;
}
void require(bool success) { if (!success) throw std::runtime_error(SDL_GetError()); }
struct SdlSession {
    SdlSession() { require(SDL_Init(SDL_INIT_VIDEO)); }
    ~SdlSession() { SDL_Quit(); }
    SdlSession(const SdlSession&) = delete;
    SdlSession& operator=(const SdlSession&) = delete;
};
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 4) {
            std::cerr << "Usage: rich4-resource-viewer ARCHIVE.mkf [ENTRY=1] [FRAME=0]\n"
                      << "Left/right: frame; PageUp/PageDown: resource; Escape: quit.\n";
            return 2;
        }
        const rich4::assets::Archive archive(argv[1]);
        std::size_t entry = argc > 2 ? number(argv[2]) : 1;
        std::size_t frame = argc > 3 ? number(argv[3]) : 0;
        auto resource = archive.decode(entry);
        auto graphics = rich4::assets::parse_graphics(resource);
        auto image = rich4::graphics::decode_frame(resource, frame);
        const SdlSession session;
        const auto window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>(
            SDL_CreateWindow("Rich4 resource viewer", 960, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY), SDL_DestroyWindow);
        if (!window) throw std::runtime_error(SDL_GetError());
        const auto renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>(SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        require(SDL_SetRenderVSync(renderer.get(), 1));
        require(SDL_SetRenderLogicalPresentation(renderer.get(), 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        const auto texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>(
            SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480), SDL_DestroyTexture);
        if (!texture) throw std::runtime_error(SDL_GetError());
        require(SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST));
        bool running = true, dirty = true;
        while (running) {
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_ESCAPE) running = false;
                    if (event.key.key == SDLK_RIGHT && frame + 1 < graphics.entries.size()) { ++frame; dirty = true; }
                    if (event.key.key == SDLK_LEFT && frame > 0) { --frame; dirty = true; }
                    if (event.key.key == SDLK_PAGEDOWN || event.key.key == SDLK_PAGEUP) {
                        const bool forward = event.key.key == SDLK_PAGEDOWN;
                        for (std::size_t candidate = entry; forward ? candidate + 1 < archive.size() : candidate > 0;) {
                            candidate = forward ? candidate + 1 : candidate - 1;
                            auto next = archive.decode(candidate);
                            try {
                                auto parsed = rich4::assets::parse_graphics(next);
                                if (parsed.entries.empty()) continue;
                                auto decoded = rich4::graphics::decode_frame(next, 0);
                                entry = candidate; frame = 0; resource = std::move(next); graphics = std::move(parsed); image = std::move(decoded); dirty = true;
                                break;
                            } catch (const rich4::assets::AssetError&) { /* Non-image resources are skipped. */ }
                        }
                    }
                }
            }
            if (dirty) {
                image = rich4::graphics::decode_frame(resource, frame);
                auto canvas = rich4::graphics::make_canvas(640, 480);
                // Viewer uses the frame's bounding box, not its in-game anchor.
                rich4::graphics::blit(canvas, image, image.origin_x, image.origin_y, rich4::graphics::Transparency::opaque);
                const auto pixels = rich4::graphics::to_argb8888(canvas);
                require(SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), 640 * 4));
                const auto title = "Rich4 resources — " + std::to_string(entry) + " / " + std::to_string(frame);
                require(SDL_SetWindowTitle(window.get(), title.c_str()));
                dirty = false;
            }
            require(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255));
            require(SDL_RenderClear(renderer.get()));
            require(SDL_RenderTexture(renderer.get(), texture.get(), nullptr, nullptr));
            require(SDL_RenderPresent(renderer.get()));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "resource viewer: " << error.what() << '\n';
        return 1;
    }
}
