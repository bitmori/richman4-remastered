// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_audio.hpp"
#include "native_frame.hpp"
#include "native_stock_names.hpp"
#include "native_ui.hpp"
#include "screens/finance.hpp"
#include "screens/main_screen.hpp"
#include "screens/menus.hpp"
#include "rich4/assets.hpp"
#include "screens/items.hpp"
#include "screens/messages.hpp"
#include "rich4/board_renderer.hpp"
#include "rich4/calendar.hpp"
#include "rich4/game.hpp"
#include "rich4/services.hpp"
#include "rich4/stocks.hpp"
#include "rich4/trading.hpp"
#include "rich4/world.hpp"
#include "rich4/world_validation.hpp"
#include "screens/effects.hpp" // EFFECTS: original board animations
#include "screens/intro.hpp"   // new-game airplane / parachute intro
#include "screens/spinners.hpp" // SPINNERS: god slot machine / cupid wheels
#include "screens/ai_settings.hpp" // 託管AI window

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __ANDROID__
#include "rich4_android.hpp" // android/app/src/main/cpp
#endif

#include <array>
#include <bit>
#include <chrono>
#include <ctime>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>

namespace {
using namespace rich4;
namespace fs = std::filesystem;
namespace messages = native::messages;
namespace spinners = native::spinners;
using native::Artwork;
using native::Button;
using native::map_names;
using native::names;
namespace screens = native::screens;

void require(bool success) {
    if (!success)
        throw std::runtime_error(SDL_GetError());
}
std::string quoted(std::string_view text) {
    std::string result = "\"";
    for (const auto byte : text) {
        if (byte == '\"' || byte == '\\') {
            result += '\\';
            result += byte;
        } else if (byte == '\n')
            result += "\\n";
        else if (static_cast<unsigned char>(byte) >= 32)
            result += byte;
    }
    return result + '"';
}

struct Options {
    fs::path assets, font, load, save_directory, output;
    bool headless = false, start_new = false, hidden_window = false, exercise_recovery = false;
    bool exercise_trading = false, overlay_gallery = false;
    bool play_effects = false; // EFFECTS (dev): play effects in replays and dump effect-NNN.ppm frames
    bool touch = false; // finger-sized number pad (always on Android; --touch on desktop)
    fs::path gallery;   // dev: render every original-style menu/finance screen to DIR and exit
    bool items_gallery = false; // dev: render every card/tool/shop screen into OUTPUT/gallery
    std::string ui_preset;          // dev: initial main-screen state, e.g. "view=2,tab=1,calendar=1,map=1"
    unsigned capture_every = 0;     // dev: also dump every Nth rendered frame into --output
    bool start_loaded = false;      // dev: open --load straight into play (no menu) in real time
    unsigned auto_rounds = 0;
    unsigned map = 0;
    unsigned exercise_card = 0, exercise_tool = 0;
    std::uint32_t seed = static_cast<std::uint32_t>(std::time(nullptr));
};

Options arguments(int argc, char **argv) {
    const fs::path home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    Options options;
    options.assets = RICH4_DEFAULT_ASSETS;
    options.save_directory = home / "Library/Application Support/Rich4Native/saves";
#ifdef __ANDROID__
    const auto platform = android_platform::prepare(); // extracted APK assets, system/bundled font, SDL pref path
    options.assets = platform.assets;
    options.font = platform.font;
    options.save_directory = platform.save_directory;
    options.touch = true;
#endif
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--headless")
            options.headless = true;
        else if (argument == "--hidden-window")
            options.hidden_window = true;
        else if (argument == "--start-loaded")
            options.start_loaded = true;
        else if (argument == "--exercise-recovery")
            options.exercise_recovery = true;
        else if (argument == "--exercise-trading")
            options.exercise_trading = true;
        else if (argument == "--play-effects")
            options.play_effects = true;
        else if (argument == "--overlay-gallery") // Dev: dump sample bubbles/boxes/news as gallery-*.ppm
            options.overlay_gallery = true;
        else if (argument == "--items-gallery")
            options.items_gallery = true;
        else if (argument == "--start-new")
            options.start_new = true;
        else if (argument == "--touch")
            options.touch = true;
        else {
            if (index + 1 >= argc)
                throw std::invalid_argument("Missing value for " + argument);
            const std::string value = argv[++index];
            if (argument == "--assets")
                options.assets = value;
            else if (argument == "--font")
                options.font = value;
            else if (argument == "--load")
                options.load = value;
            else if (argument == "--save-dir")
                options.save_directory = value;
            else if (argument == "--output")
                options.output = value;
            else if (argument == "--auto-rounds")
                options.auto_rounds = static_cast<unsigned>(std::stoul(value));
            else if (argument == "--seed")
                options.seed = static_cast<std::uint32_t>(std::stoul(value));
            else if (argument == "--map")
                options.map = static_cast<unsigned>(std::stoul(value));
            else if (argument == "--exercise-card")
                options.exercise_card = static_cast<unsigned>(std::stoul(value));
            else if (argument == "--ui-preset")
                options.ui_preset = value;
            else if (argument == "--capture-every")
                options.capture_every = static_cast<unsigned>(std::stoul(value));
            else if (argument == "--exercise-tool")
                options.exercise_tool = static_cast<unsigned>(std::stoul(value));
            else if (argument == "--gallery")
                options.gallery = value;
            else
                throw std::invalid_argument("Unknown argument: " + argument);
        }
    }
    if (options.headless && !options.auto_rounds)
        options.auto_rounds = 3;
    if (options.map > 7)
        throw std::invalid_argument("Map must be 0..7");
    if (options.exercise_card > 30 || options.exercise_tool > 13)
        throw std::invalid_argument("Invalid requested interaction ID");
    if (!options.gallery.empty()) {
        options.headless = true;
        options.gallery = fs::absolute(options.gallery);
    }
    if (options.headless && options.output.empty() && options.gallery.empty())
        throw std::invalid_argument("Headless testing requires --output DIR");
    if (options.font.empty()) {
        const std::array candidates{
            fs::path("/Applications/Microsoft Word.app/Contents/Resources/DFonts/mingliu.ttc"),
            home / "Library/Containers/org.boxedwine/Data/Library/Application "
                   "Support/Boxedwine/Rich4/root/home/username/.wine/drive_c/windows/Fonts/mingliu.ttc",
            fs::path("/System/Library/Fonts/PingFang.ttc")};
        for (const auto &path : candidates)
            if (fs::is_regular_file(path)) {
                options.font = path;
                break;
            }
    }
    if (options.font.empty())
        throw std::runtime_error("No Chinese font found; specify --font FONT.ttf");
    options.save_directory = fs::absolute(options.save_directory);
    if (!options.output.empty())
        options.output = fs::absolute(options.output);
    return options;
}

// Append only: the values are logged in inputs.jsonl and name the screen-N.ppm captures.
enum class Screen {
    menu,
    new_game,
    game,
    settings,
    wealth,
    cards,
    tools,
    stocks,
    trading,
    load,
    save,
    ai_settings
};

class Application {
  public:
    explicit Application(Options options)
        : options_(std::move(options)), artwork_(options_.assets), font_(options_.font) {
        selected_map_ = options_.map % 4;
        selected_stage_ = options_.map / 4;
        if (options_.headless) {
            require(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
            require(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
        }
        if (options_.hidden_window)
            require(SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1"));
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
        window_.reset(
            SDL_CreateWindow("大富翁 4", 960, 720,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                 ((options_.headless || options_.hidden_window) ? SDL_WINDOW_HIDDEN : 0)));
        if (!window_)
            throw std::runtime_error(SDL_GetError());
#ifdef __ANDROID__
        SDL_SetWindowFullscreen(window_.get(), true); // immersive: hide status and navigation bars
        require(SDL_AddEventWatch(save_on_background, this));
#endif
        renderer_.reset(SDL_CreateRenderer(window_.get(), options_.headless ? "software" : nullptr));
        if (!renderer_)
            throw std::runtime_error(SDL_GetError());
        if (!options_.headless)
            SDL_SetRenderVSync(renderer_.get(), 1);
        require(
            SDL_SetRenderLogicalPresentation(renderer_.get(), 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        texture_.reset(SDL_CreateTexture(renderer_.get(), SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, 640, 480));
        if (!texture_)
            throw std::runtime_error(SDL_GetError());
        require(SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_NEAREST));
        if (options_.headless && std::string(SDL_GetCurrentVideoDriver()) != "dummy")
            throw std::runtime_error("Native headless verification must use the dummy video driver");
        if (!options_.output.empty()) {
            fs::create_directories(options_.output);
            events_.open(options_.output / "events.jsonl");
            inputs_.open(options_.output / "inputs.jsonl");
        }
        audio_ = std::make_unique<audio::Audio>(options_.assets);
        pad_.set_scale(options_.touch ? 2 : 1);
        effects_.set_skip(options_.auto_rounds && !options_.play_effects); // EFFECTS: replays stay zero-duration
        spinners_.set_skip(options_.auto_rounds && !options_.play_effects); // SPINNERS: same as effects
        messages_.set_instant(options_.auto_rounds != 0); // Headless replay never waits on a bubble.
        main_screen_.set_skip_toss(options_.auto_rounds && !options_.play_effects); // replays stay fast
        items_.sound = [this](std::size_t effect) {
            if (sound_effects_)
                audio_->play_sound(effect);
        };
        if (!audio_->music_available())
            std::cerr << audio_->music_diagnostic() << '\n';
    }
    ~Application() {
        audio_.reset();
        texture_.reset();
        renderer_.reset();
        window_.reset();
        SDL_Quit();
    }

    void write_diagnostic(std::string_view reason) const {
        if (options_.output.empty() || !session_)
            return;
        ui::write_file(options_.output / "failure-world.DAT", session_->world().export_save());
        std::ofstream output(options_.output / "failure.json");
        output << "{\"resumable_save\":false,\"reason\":" << quoted(reason)
               << ",\"phase\":" << int(session_->phase()) << ",\"random_state\":" << session_->random_state()
               << ",\"completed_days\":" << session_->completed_days() << ",\"decision\":"
               << quoted(session_->pending_decision() ? session_->pending_decision()->prompt : "") << "}\n";
    }

    int run() {
        if (options_.items_gallery) {
            if (options_.output.empty())
                throw std::invalid_argument("--items-gallery requires --output DIR");
            screens::NewGameChoice choice; // 小丹尼 (human) and three computer opponents
            choice.players = {{10, 1}, {0, 2}, {1, 2}, {2, 2}};
            new_game(choice);
            fs::create_directories(options_.output / "gallery");
            items_.gallery(
                frame_, session_->world(),
                [this] {
                    buttons_.clear();
                    canvas_ = graphics::make_canvas(640, 480, ui::color(7, 30, 55));
                    draw_board();
                },
                [this](const std::string &name) {
                    graphics::write_ppm(canvas_, options_.output / "gallery" / (name + ".ppm"));
                });
            return 0;
        }
        if (!options_.gallery.empty())
            return gallery();
        if (options_.start_loaded && !options_.load.empty() && !options_.auto_rounds)
            load(options_.load);
        auto last_tick = SDL_GetTicks();
        for (unsigned iteration = 0; running_; ++iteration) {
            draw();
            if (options_.auto_rounds && !effects_.active() && !intro_.active()) // replay input waits for movies
                drive_test();
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (intro_.active() && screen_ == Screen::game) {
                    // The intro movies are skippable by mouse-up or key-up (fcn_0045144f flags bit 1);
                    // a skip ends the whole intro. Tap and Android Back skip too. Nothing else
                    // reacts to input until it ends. Only a release whose press happened during the
                    // intro counts: the release of the setup's OK press must not skip it at once.
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                        (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat))
                        intro_pressed_ = true;
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
                        frame_.pointer_down = false; // no stale "pressed" control after the intro
                    if ((event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_KEY_UP) &&
                        intro_pressed_) {
                        intro_.skip();
                        continue;
                    }
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                        event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_MOUSE_MOTION ||
                        event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_TEXT_INPUT ||
                        event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_UP ||
                        event.type == SDL_EVENT_FINGER_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL)
                        continue;
                }
                if (event.type == SDL_EVENT_QUIT) {
                    if (screen_ == Screen::stocks)
                        close_stocks();
                    else if (screen_ == Screen::trading)
                        close_trading();
                    running_ = false;
                    continue;
                }
                if (event.type == SDL_EVENT_TEXT_INPUT && editing_amount_) {
                    for (const char *ch = event.text.text; *ch; ++ch)
                        if (*ch >= '0' && *ch <= '9' && amount_text_.size() < 10)
                            amount_text_ += *ch;
                    update_amount();
                }
                if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    require(SDL_ConvertEventToRenderCoordinates(renderer_.get(), &event));
                    const bool motion = event.type == SDL_EVENT_MOUSE_MOTION;
                    frame_.pointer_x = int(motion ? event.motion.x : event.button.x);
                    frame_.pointer_y = int(motion ? event.motion.y : event.button.y);
                    if (!motion)
                        frame_.pointer_down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    if (screen_ == Screen::game && session_) {
                        if (motion)
                            main_screen_.pointer_motion(frame_.pointer_x, frame_.pointer_y,
                                                        (event.motion.state & SDL_BUTTON_LMASK) != 0);
                        else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
                            main_screen_.pointer_up(frame_.pointer_x, frame_.pointer_y,
                                                    event.button.button == SDL_BUTTON_RIGHT);
                    }
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT &&
                    cancellable()) {
                    buttons_.clear();
                    cancel_screen(); // The original's right-button-up means cancel/back on these screens.
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT &&
                    screen_ == Screen::game && session_ && !messages_.active() && session_->pending_decision() &&
                    messages::is_yes_no(*session_->pending_decision())) {
                    buttons_.clear();
                    respond(yes_no_index(0)); // Right-click answers NO, like _rich4_ui_yesno.
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                    const int x = int(event.button.x), y = int(event.button.y);
                    for (const auto &button : buttons_)
                        if (button.enabled && ui::contains(button.rectangle, x, y)) {
                            auto action = button.action;
                            ++input_events_;
                            if (inputs_)
                                inputs_ << "{\"button\":" << button.id << ",\"screen\":" << int(screen_)
                                        << ",\"day\":" << (session_ ? session_->completed_days() : 0)
                                        << "}\n";
                            if (sound_effects_ && button.click_sound && button.id != messages::skip_button)
                                audio_->play_sound(9);
                            buttons_.clear(); // Remaining events cannot invoke controls from the previous
                                              // screen/decision.
                            action();
                            break;
                        }
                }
#ifdef __ANDROID__
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_AC_BACK)
                    event.key.key = SDLK_ESCAPE; // system Back behaves like Escape
#endif
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && screen_ == Screen::game &&
                    messages_.active() && !editing_amount_) {
                    buttons_.clear();
                    messages_.skip(); // Any key skips a bubble/box/news, as in the original.
                    continue;
                }
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    buttons_.clear();
                    if (pad_.active()) {
                        pad_key(event.key.key);
                        continue;
                    }
                    if (event.key.key == SDLK_ESCAPE && cancellable()) {
                        cancel_screen();
                        continue;
                    }
                    if (editing_amount_) {
                        if (event.key.key == SDLK_BACKSPACE) {
                            if (!amount_text_.empty())
                                amount_text_.pop_back();
                            update_amount();
                        }
                        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_ESCAPE)
                            end_amount_edit();
                        continue;
                    }
                    if (event.key.key == SDLK_ESCAPE) {
                        if (screen_ == Screen::stocks)
                            close_stocks();
                        else if (screen_ == Screen::trading)
                            close_trading();
                        else
                            screen_ = screen_ == Screen::game ? Screen::menu
                                      : session_              ? Screen::game
                                                              : Screen::menu;
                    }
                    if ((event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN) &&
                        screen_ == Screen::game && session_) {
                        if (session_->phase() == GamePhase::human_ready)
                            session_->roll_dice();
                        else if (session_->pending_decision() && presentation_.empty() &&
                                 !presenting()) { // never answer a decision that is not shown yet
                            if (map_decision()) {
                                if (selected_target_)
                                    respond(*selected_target_);
                            } else
                                respond(0);
                        }
                    }
                    if (event.key.key == SDLK_LEFT && screen_ == Screen::game)
                        rotate(7);
                    if (event.key.key == SDLK_RIGHT && screen_ == Screen::game)
                        rotate(1);
                    if (event.key.key == SDLK_V && screen_ == Screen::game)
                        main_screen_.cycle_view_mode();
                    if (event.key.key == SDLK_F5 && session_ && session_->can_save())
                        save();
                    if (event.key.key == SDLK_F9 && fs::exists(load_path()))
                        load(load_path());
                }
            }
            const auto now = SDL_GetTicks();
            const unsigned elapsed =
                options_.auto_rounds
                    ? (options_.play_effects && session_ && // dev: real-time toss/dice/spinner frame strips
                               (main_screen_.dice_busy() || session_->phase() == GamePhase::rolling ||
                                (session_->pending_decision() && spinners::handles(*session_->pending_decision())))
                           ? 40
                           : 500)
                                     : static_cast<unsigned>(std::min<Uint64>(now - last_tick, 100)) * speed_;
            last_tick = now;
            if (session_ && screen_ == Screen::game && !items_.popup_active()) { // the card popup is modal
                observe(); // a GO press emits the roll before the clock advances
                if (!effects_.active() && !messages_.active() && !intro_.active()) // only the step shown advances
                    main_screen_.advance(elapsed);
                pump_presentation();
                // The original roll animation, board effects (EFFECTS) and every bubble/box/news popup
                // block the game loop until everything already emitted has been shown, in order.
                if (presentation_.empty() && !presenting())
                    session_->tick(elapsed);
                observe();
                if (session_->can_save() && last_autosaved_day_ != session_->completed_days()) {
                    write_save(options_.save_directory / "Autosave.DAT");
                    last_autosaved_day_ = session_->completed_days();
                }
            }
            if (options_.auto_rounds && iteration > 50000)
                throw std::runtime_error("Native UI replay exceeded its operation limit");
            audio_->update();
            if (!options_.auto_rounds)
                SDL_Delay(8);
        }
        // Quitting writes the AUTO slot: the manual SAVE1..5 slots only change when the player saves.
        if (session_ && session_->can_save() && !options_.auto_rounds)
            write_save(slot_path(0));
        return final_status_;
    }

  private:
#ifdef __ANDROID__
    // Android may kill a backgrounded app without SDL_EVENT_QUIT, and SDL blocks the main loop
    // while paused, so lifecycle events must be handled here (SDL calls watches synchronously
    // on the main thread from the event pump).
    static bool SDLCALL save_on_background(void *self, SDL_Event *event) {
        if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND || event->type == SDL_EVENT_TERMINATING) {
            try {
                auto &application = *static_cast<Application *>(self);
                if (application.session_ && application.session_->can_save()) // never overwrite manual Save1
                    application.write_save(application.options_.save_directory / "Autosave.DAT");
            } catch (const std::exception &error) { // must not unwind through SDL's C code
                std::cerr << "Background save failed: " << error.what() << '\n';
            }
        }
        return true;
    }
#endif
    void picture(const graphics::Image &image, int x, int y) {
        frame_.picture(image, x, y);
    }
    void button(int id, graphics::Rect rect, std::string_view label, std::function<void()> action,
                bool enabled = true) {
        frame_.button(id, rect, label, std::move(action), enabled);
    }
    void hit(int id, graphics::Rect rect, std::function<void()> action, bool enabled = true) {
        frame_.hit(id, rect, std::move(action), enabled);
    }
    void dialog(graphics::Rect rect, std::string_view title) {
        frame_.dialog(rect, title);
    }
    // ---- Original-style menu and finance screens (tools/screens) ----
    screens::Host make_host() {
        return {frame_,
                [this](int effect) {
                    if (sound_effects_ && effect >= 0)
                        audio_->play_sound(std::size_t(effect));
                },
                [this](std::string_view message) {
                    toast_ = message;
                    toast_until_ = SDL_GetTicks() + 2200;
                },
                options_.auto_rounds != 0 || !options_.gallery.empty()};
    }
    void pad_key(SDL_Keycode key) {
        auto host = make_host();
        if (key == SDLK_ESCAPE)
            pad_.cancel(host);
        else if (key >= SDLK_0 && key <= SDLK_9)
            pad_.key(host, char('0' + (key - SDLK_0)));
        else if (key >= SDLK_KP_1 && key <= SDLK_KP_9)
            pad_.key(host, char('1' + (key - SDLK_KP_1)));
        else if (key == SDLK_KP_0)
            pad_.key(host, '0');
        else if (key == SDLK_BACKSPACE)
            pad_.key(host, '\b');
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
            pad_.key(host, '\r');
        else if (key == SDLK_M || key == SDLK_C || key == SDLK_H)
            pad_.key(host, key == SDLK_M ? 'M' : key == SDLK_C ? 'C' : 'H');
    }
    bool bank_pending() const {
        return session_ && session_->pending_decision() &&
               session_->pending_decision()->kind == DecisionKind::bank;
    }
    // Screens where right-click / Escape / Android Back mean "cancel" in the original.
    bool cancellable() const {
        return pad_.active() || screen_ == Screen::new_game || screen_ == Screen::settings ||
               screen_ == Screen::wealth || screen_ == Screen::stocks || screen_ == Screen::load ||
               screen_ == Screen::trading || screen_ == Screen::ai_settings ||
               screen_ == Screen::save || (screen_ == Screen::game && bank_pending());
    }
    void cancel_screen() {
        auto host = make_host();
        if (pad_.active()) {
            pad_.cancel(host);
            return;
        }
        switch (screen_) {
        case Screen::stocks:
            stock_market_.cancel(host, stock_actions());
            break;
        case Screen::trading:
            trading_board_.cancel(host, trading_actions());
            break;
        case Screen::ai_settings:
            ai_settings_.cancel(host, ai_settings_actions());
            break;
        case Screen::new_game:
            setup_.cancel(host, [this] { screen_ = Screen::menu; });
            break;
        case Screen::settings:
        case Screen::load:
        case Screen::save:
        case Screen::wealth:
            host.sound(screens::cancel_sound);
            screen_ = back_screen();
            break;
        case Screen::game:
            if (bank_pending())
                bank_screen_.cancel(host, *session_->pending_decision(), bank_actions());
            break;
        default:
            break;
        }
    }
    // Where settings / save / load / player info return to: the menu they came from, else the game.
    Screen back_screen() const {
        return session_ && previous_screen_ != Screen::menu ? Screen::game : Screen::menu;
    }
    // Answers the pending decision with the option whose value is `value`.
    bool respond_value(std::int32_t value, std::int32_t amount) {
        const auto &decision = session_->pending_decision();
        if (!decision)
            return false;
        for (std::size_t index = 0; index < decision->options.size(); ++index)
            if (decision->options[index].value == value) {
                bank_amount_ = amount;
                const bool success = session_->respond(index, amount);
                observe();
                return success;
            }
        return false;
    }
    screens::BankActions bank_actions() {
        return {[this](std::int32_t value, std::int32_t amount) { return respond_value(value, amount); }};
    }
    screens::StockActions stock_actions() {
        return {[this](std::size_t stock, std::int32_t quantity) {
                    selected_stock_ = stock;
                    return trade(true, quantity);
                },
                [this](std::size_t stock, std::int32_t quantity) {
                    selected_stock_ = stock;
                    return trade(false, quantity);
                },
                [this] { close_stocks(); }};
    }
    // Save slots: 0 is the automatic save (load only, shown as AUTO), 1..5 are manual.
    fs::path slot_path(int slot) const {
        return options_.save_directory / (slot == 0 ? std::string("Autosave.DAT") : "Save" + std::to_string(slot) + ".DAT");
    }
    std::array<screens::SaveSlot, 6> read_slots() const {
        std::array<screens::SaveSlot, 6> slots{};
        for (int slot = 0; slot < 6; ++slot) {
            try {
                if (!fs::exists(slot_path(slot)))
                    continue;
                const auto save = SaveFile::decode(ui::read_file(slot_path(slot)));
                const auto header = save.header();
                if (header.signature != 0x26)
                    continue;
                auto &entry = slots[std::size_t(slot)];
                entry.valid = true;
                entry.date = header.date;
                entry.map = header.map_id;
                entry.stage = header.stage_id;
                for (std::size_t player = 0; player < std::min<std::uint32_t>(header.player_count, 4); ++player)
                    entry.characters.push_back(save.player(player).character);
            } catch (const std::exception &) {
            }
        }
        return slots;
    }
    bool any_slot() const {
        for (int slot = 0; slot < 6; ++slot)
            if (fs::exists(slot_path(slot)))
                return true;
        return false;
    }
    void open_load() {
        if (!options_.load.empty()) { // An imported DAT (or the replay's --load) loads directly.
            load(options_.load);
            return;
        }
        const auto slots = read_slots();
        if (std::none_of(slots.begin(), slots.end(), [](const auto &slot) { return slot.valid; })) {
            toast_ = "沒有存檔";
            toast_until_ = SDL_GetTicks() + 2200;
            return;
        }
        slots_.open(false, slots);
        screen_ = Screen::load;
    }
    void open_save() {
        if (!session_ || !session_->can_save())
            return;
        slots_.open(true, read_slots());
        screen_ = Screen::save;
    }
    void choose_slot(int slot) {
        if (slots_.saving()) {
            write_save(slot_path(slot));
            options_.load.clear();
            ++saves_;
            toast_ = "存檔完成";
            toast_until_ = SDL_GetTicks() + 2200;
            screen_ = Screen::game;
            return;
        }
        const auto previous = back_screen();
        if (!load(slot_path(slot)))
            screen_ = previous;
    }
    void notify_action(bool success, std::string_view failure = "目前無法使用") {
        if (!success) {
            toast_ = failure;
            toast_until_ = SDL_GetTicks() + 2200;
        }
        if (session_)
            observe();
    }
    void update_amount() {
        if (!editing_amount_)
            return;
        const auto value = amount_text_.empty() ? 0LL : std::stoll(amount_text_);
        *editing_amount_ = static_cast<std::int32_t>(std::clamp<long long>(value, 0, amount_limit_));
    }
    void end_amount_edit() {
        editing_amount_ = nullptr;
        SDL_StopTextInput(window_.get());
    }
    fs::path save_path() const {
        return options_.save_directory / "Save1.DAT";
    }
    fs::path load_path() const {
        if (!options_.load.empty())
            return options_.load;
        // Continue from whichever slot is newest: Android backgrounding writes only the autosave.
        auto newest = save_path();
        for (int slot = 0; slot <= 5; ++slot) {
            const auto candidate = slot_path(slot);
            if (fs::exists(candidate) &&
                (!fs::exists(newest) || fs::last_write_time(candidate) > fs::last_write_time(newest)))
                newest = candidate;
        }
        return newest;
    }
    void rotate(unsigned increment) {
        rotation_ = (rotation_ + increment) % 8;
        if (session_)
            session_->set_camera_rotation(static_cast<std::uint8_t>(rotation_));
    }
    void open_setup(unsigned stage) {
        setup_.open(stage, stage == selected_stage_ ? selected_map_ : 0, options_.seed);
        screen_ = Screen::new_game;
    }
    // Picked characters are humans in click order, the auto-filled rest are computers
    // (rich4_new_game.asm). The original picks computers with the gameplay rand(); the
    // native setup uses its own generator seeded from --seed instead.
    void new_game(const screens::NewGameChoice &choice) {
        options_.load.clear();
        NewGameOptions configuration;
        const auto time = std::time(nullptr);
        const auto local = *std::localtime(&time);
        configuration.date = {static_cast<std::uint8_t>(local.tm_mday),
                              static_cast<std::uint8_t>(local.tm_mon + 1),
                              static_cast<std::uint16_t>(local.tm_year + 1900)};
        selected_map_ = choice.map;
        selected_stage_ = choice.stage;
        configuration.map_id = static_cast<std::uint16_t>(choice.map);
        configuration.stage_id = static_cast<std::uint16_t>(choice.stage);
        configuration.initial_fund = choice.fund;
        configuration.lease_mode = choice.lease_mode;
        configuration.random_seed = options_.seed;
        configuration.players = choice.players;
        try {
            install_session(World::from_map_resource(
                                artwork_.archive('m').decode((choice.stage * 4 + choice.map) * 2 + 1), configuration),
                            options_.seed, true);
        } catch (const std::exception &error) {
            std::cerr << "New game failed: " << error.what() << '\n';
            toast_ = "無法開始新遊戲";
            toast_until_ = SDL_GetTicks() + 3500;
            screen_ = Screen::menu;
            return;
        }
        ++new_games_;
    }
    // intro: a new game plays the original airplane/parachute intro before the first turn
    // (rich4_main.asm:376 fcn_00415872); loading a save does not (loc_00401d08).
    void install_session(World world, std::uint32_t seed, bool intro = false) {
        validate_playable_world(world);
        end_amount_edit();
        const auto map = static_cast<std::uint8_t>(world.header.stage_id * 4 + world.header.map_id);
        auto session = std::make_unique<GameSession>(std::move(world), seed);
        auto board = std::make_unique<board::Renderer>(options_.assets, map);
        session_ = std::move(session);
        board_ = std::move(board);
        if (!initial_random_state_)
            initial_random_state_ = session_->random_state();
        rotation_ = session_->world().camera_rotation % 8;
        screen_ = Screen::game;
        last_autosaved_day_ = UINT64_MAX;
        bank_done_for_visit_ = false;
        messages_.clear();
        spinners_.clear();
        presentation_.clear();
        presented_player_.reset();
        items_.reset();
        auction_room_.reset();
        intro_.skip();
        if (intro && !(options_.auto_rounds && !options_.play_effects)) { // replays keep their timing
            std::vector<std::uint8_t> characters;
            for (const auto &player : session_->world().players)
                if (player.state.controller_flags)
                    characters.push_back(player.state.character);
            intro_.start(characters);
            intro_ticks_ = 0;
            intro_pressed_ = false;
        }
        observe(); // the first turn's events queue behind the intro (presenting())
    }
    bool load(const fs::path &path) {
        try {
            const auto bytes = ui::read_file(path);
            auto seed = options_.seed;
            std::ifstream state(path.string() + ".rng");
            std::string signature;
            std::uint32_t stored_seed = 0;
            if (state >> signature >> stored_seed && signature == "RICH4-RNG-1")
                seed = stored_seed;
            install_session(World::from_save(SaveFile::decode(bytes)), seed);
            ++loads_;
            load_failed_ = false;
            toast_ = "讀取完成";
            toast_until_ = SDL_GetTicks() + 2200;
            return true;
        } catch (const std::exception &error) {
            load_failed_ = true;
            toast_ = "無法讀取此存檔";
            toast_until_ = SDL_GetTicks() + 3500;
            std::cerr << "Load failed: " << error.what() << '\n';
            return false;
        }
    }
    void save() {
        if (!session_ || !session_->can_save())
            return;
        write_save(save_path());
        options_.load.clear(); // An imported DAT never shadows this newly saved native slot.
        ++saves_;
        toast_ = "存檔完成";
        toast_until_ = SDL_GetTicks() + 2200;
    }
    void write_save(const fs::path &path) {
        ui::write_file(path, session_->export_save().encode());
        const auto state = "RICH4-RNG-1 " + std::to_string(session_->random_state()) + '\n';
        ui::write_file(path.string() + ".rng", {state.begin(), state.end()});
    }
    // One tick of the core can emit a whole chain (e.g. a jailed player's notice, the next turn and
    // that player's roll). The original plays these strictly one after another, so presentation
    // (bubbles/boxes, dice, effect movies, sounds) goes through one ordered queue, and the core
    // clock only advances once the queue is empty. Bookkeeping for the replay harness stays immediate.
    struct PresentationStep {
        std::optional<GameEvent> event;
        std::optional<EffectEvent> effect;
    };
    bool presenting() const {
        return intro_.active() || main_screen_.dice_busy() || effects_.active() || messages_.active() ||
               spinners_.active() || auction_room_.sold_active() || items_.magic_cast_active();
    }
    void pump_presentation() {
        while (session_ && !presentation_.empty() && !presenting()) {
            auto step = std::move(presentation_.front());
            presentation_.pop_front();
            if (step.effect && spinners::handles(*step.effect))
                spinners_.enqueue(std::move(*step.effect)); // SPINNERS: computer spin / wheel slow-down
            else if (step.effect)
                effects_.enqueue({std::move(*step.effect)}, session_.get()); // EFFECTS
            else
                present(*step.event);
        }
    }
    void present(const GameEvent &event) {
        if (event.kind == GameEventKind::turn_started)
            presented_player_ = event.player;
        main_screen_.on_event(event, *session_, artwork_);
        if (auto item = messages::from_event(event, session_->world()))
            messages_.push(std::move(*item)); // Voices play when their bubble appears.
        if (event.kind == GameEventKind::notice && event.message == "拍卖成交" && event.subject >= 0 &&
            !(options_.auto_rounds && !options_.play_effects)) // AUCTION: sold sequence (replays stay fast)
            auction_room_.start_sold(std::size_t(event.subject), event.amount, event.property);
        if (event.kind == GameEventKind::notice && event.message == "魔法屋施法" && // MAGIC: computer cast
            !(options_.auto_rounds && !options_.play_effects))
            items_.start_magic_cast(event.second_subject, event.value);
        if (!sound_effects_)
            return;
        switch (event.kind) { // the dice sound fires at frame 30 of the dice FLC (MainScreen)
        case GameEventKind::land_purchased:
            audio_->play_sound(49);
            break;
        case GameEventKind::land_upgraded:
            audio_->play_sound(50);
            break;
        case GameEventKind::rent_paid:
            audio_->play_sound(55);
            break;
        default:
            break;
        }
    }
    void observe() {
        auto effects = session_->drain_effects();
        auto events = session_->drain_events();
        std::size_t next_effect = 0;
        const auto queue_effects_before = [&](std::size_t event_index) {
            while (next_effect < effects.size() && effects[next_effect].after_events <= event_index)
                presentation_.push_back({std::nullopt, std::move(effects[next_effect++])});
        };
        for (std::size_t index = 0; index < events.size(); ++index) {
            const auto &event = events[index];
            queue_effects_before(index);
            presentation_.push_back({event, std::nullopt});
            ++event_counts_[event.kind];
            if (event.kind == GameEventKind::dice_rolled && event.player < dice_by_player_.size())
                ++dice_by_player_[event.player];
            if (event.kind == GameEventKind::unsupported_event)
                unsupported_.push_back(event.message);
            items_.observe(event, session_->world());
            if (event.kind == GameEventKind::notice && event.message.starts_with("使用")) {
                for (unsigned i = 0; i < services::card_names.size(); ++i)
                    if (event.message == "使用" + std::string(services::card_names[i])) {
                        ++cards_used_;
                        card_ids_used_.insert(i + 1);
                    }
                for (unsigned i = 0; i < services::tool_names.size(); ++i)
                    if (event.message == "使用" + std::string(services::tool_names[i])) {
                        ++tools_used_;
                        tool_ids_used_.insert(i + 1);
                    }
            }
            if (event.kind == GameEventKind::date_changed || event.kind == GameEventKind::turn_started) {
                bank_done_for_visit_ = false;
                shop_done_for_visit_ = false;
                tested_cards_ = tested_tools_ = false;
                tested_dice_ = false;
            }
            if (event.kind != GameEventKind::moved && !event.message.empty()) {
                recent_.push_back(event.message);
                if (recent_.size() > 5)
                    recent_.erase(recent_.begin());
            }
            if (events_)
                events_ << "{\"event\":" << int(event.kind) << ",\"player\":" << event.player
                        << ",\"node\":" << event.node << ",\"amount\":" << event.amount
                        << ",\"day\":" << session_->completed_days()
                        << ",\"message\":" << quoted(event.message) << "}\n";
        }
        queue_effects_before(events.size());
        pump_presentation();
    }
    void draw() {
        buttons_.clear();
        messages_.retire();
        canvas_ = graphics::make_canvas(640, 480, ui::color(7, 30, 55));
        frame_.ticks = SDL_GetTicks();
        if (screen_ != last_screen_) {
            // Screens may be entered from anywhere (menu, toolbar, keys): (re)initialise on entry.
            previous_screen_ = last_screen_;
            last_screen_ = screen_;
            pad_.close();
            if (screen_ == Screen::settings)
                options_screen_.open({speed_, audio_->music_enabled(), sound_effects_}, session_ != nullptr);
            if (screen_ == Screen::wealth && session_)
                info_.open(session_->world().header.current_player);
            if (screen_ == Screen::ai_settings &&
                (!session_ || !ai_settings_.open(*session_, presented_player())))
                screen_ = last_screen_ = Screen::game; // nobody to hand over (all-computer game)
        }
        if (!bank_pending())
            bank_screen_.reset();
        auto host = make_host();
        if (session_)
            items_.run_deferred();
        items_.shot.clear();
        if (options_.auto_rounds)
            items_.clock = rendered_frames_ * 250;
        if (session_) // EFFECTS: pre-mutation board for effect commit frames (only while idle)
            effects_.remember(session_->world(), session_.get(), frame_.ticks);
        const bool menu_backdrop = screen_ == Screen::menu || ((screen_ == Screen::settings || screen_ == Screen::load) &&
                                                               previous_screen_ == Screen::menu) ||
                                   !session_;
        if (menu_backdrop && screen_ != Screen::new_game) {
            if (screen_ != Screen::menu)
                picture(artwork_.frame('d', 1, 0), 0, 0);
        } else if (session_ && screen_ != Screen::new_game)
            draw_board();
        if (session_ && screen_ == Screen::game && !items_.popup_active()) { // card art plays before its movie
            draw_effects(); // EFFECTS: overlay after the board, before toasts
            if (!effects_.active()) // SPINNERS: computer spins / wheel slow-down, in presentation order
                spinners_.draw_effect(frame_, session_->world(), spinner_elapsed(), spinners::skip_button);
        }
        if (session_ && screen_ == Screen::game)
            draw_intro();
        if (screen_ == Screen::menu) {
            main_menu_.draw(host, {[this] {
                                       if (session_ && session_->phase() != GamePhase::finished)
                                           screen_ = Screen::game;
                                       else
                                           open_setup(0);
                                   },
                                   [this] { open_load(); },
                                   [this] { screen_ = Screen::settings; },
                                   [this] { running_ = false; },
                                   [this] { open_setup(1); },
                                   fs::exists(load_path()) || any_slot()});
        } else if (screen_ == Screen::new_game) {
            setup_.draw(host, [this](const screens::NewGameChoice &choice) { new_game(choice); },
                        [this] { screen_ = Screen::menu; });
        } else if (screen_ == Screen::settings) {
            buttons_.clear();
            options_screen_.draw(
                host, {[this](const screens::OptionsState &state) {
                           speed_ = state.speed;
                           if (audio_->music_enabled() != state.music)
                               audio_->set_music(state.music);
                           sound_effects_ = state.sound;
                       },
                       [this] { screen_ = back_screen(); },
                       [this](unsigned track) {
                           if (!audio_->music_enabled())
                               audio_->set_music(true);
                           audio_->select_music(track);
                       },
                       audio_->music_enabled() ? audio_->music_track() : 0,
                       [this] { open_setup(selected_stage_); },
                       [this] { running_ = false; }});
        } else if (screen_ == Screen::load || screen_ == Screen::save) {
            buttons_.clear();
            slots_.draw(host, [this](int slot) { choose_slot(slot); }, [this] { screen_ = back_screen(); });
        } else if (screen_ == Screen::wealth) {
            buttons_.clear();
            info_.draw(host, session_->world(), [this] { screen_ = Screen::game; });
        } else if (screen_ == Screen::cards || screen_ == Screen::tools) {
            draw_inventory();
        } else if (screen_ == Screen::stocks) {
            draw_stocks();
        } else if (screen_ == Screen::trading) {
            draw_trading();
        } else if (screen_ == Screen::ai_settings && session_) {
            buttons_.clear();
            ai_settings_.draw(host, session_->world(), ai_settings_actions());
        }
        std::string overlay_key;
        if (screen_ == Screen::game && session_ && messages_.active()) {
            const auto kind = messages_.current()->kind;
            if (messages_.draw(frame_)) {
                static constexpr std::array<const char *, 5> kinds{"bubble", "box", "news", "fortune", "god"};
                overlay_key = std::string("overlay-") + kinds.at(static_cast<std::size_t>(kind));
            }
        }
        if (screen_ == Screen::game && session_ && items_.magic_cast_active() && !messages_.active()) {
            buttons_.clear(); // MAGIC: a computer's magic-house cast and Panel 20 movie
            items_.draw_magic_cast(frame_);
        }
        if (screen_ == Screen::game && session_ && auction_room_.sold_active() && !messages_.active()) {
            buttons_.clear(); // AUCTION: the sold sequence covers the board until it ends or is tapped
            auction_room_.draw_sold(host, session_->world());
        }
        if (items_.popup_active() && session_ &&
            (screen_ == Screen::game || screen_ == Screen::cards || screen_ == Screen::tools)) {
            buttons_.clear();
            items_.draw_popup(frame_);
        }
        if (pad_.active()) { // The number pad is modal over every screen.
            buttons_.clear();
            pad_.draw(host);
        }
        if (!toast_.empty() && SDL_GetTicks() < toast_until_) {
            ui::fill(canvas_, {210, 439, 430, 474}, ui::paper);
            ui::border(canvas_, {210, 439, 430, 474}, ui::blue);
            font_.draw(canvas_, toast_, 320 - font_.measure(toast_) / 2, 447);
        }
        spinners_.finish_frame(); // SPINNERS: rattle loop only while a reel/wheel is visibly spinning
        const auto pixels = graphics::to_argb8888(canvas_);
        require(SDL_UpdateTexture(texture_.get(), nullptr, pixels.data(), 640 * 4));
        require(SDL_SetRenderDrawColor(renderer_.get(), 0, 0, 0, 255));
        require(SDL_RenderClear(renderer_.get()));
        require(SDL_RenderTexture(renderer_.get(), texture_.get(), nullptr, nullptr));
        require(SDL_RenderPresent(renderer_.get()));
        ++rendered_frames_;
        if (options_.capture_every && !options_.output.empty() && rendered_frames_ % options_.capture_every == 0) {
            std::ostringstream name;
            name << "frame-" << std::setw(6) << std::setfill('0') << rendered_frames_ << ".ppm";
            graphics::write_ppm(canvas_, options_.output / name.str());
        }
        if (options_.auto_rounds && !options_.output.empty() && !gallery_) {
            auto key = "screen-" + std::to_string(static_cast<int>(screen_));
            if (screen_ == Screen::game && session_ && session_->pending_decision())
                key = "decision-" + std::to_string(static_cast<int>(session_->pending_decision()->kind)) +
                      (selected_target_ ? "-selected" : "") +
                      (bank_pending() ? bank_screen_.at_atm() ? "-atm" : bank_screen_.at_desk() ? "-desk" : "-director"
                                      : "");
            if (pad_.active())
                key += "-pad";
            if (!overlay_key.empty()) // First few of each overlay kind, for visual review.
                key = overlay_key + "-" + std::to_string(std::min(++overlay_captures_[overlay_key], 4U));
            if (captured_frames_.insert(key).second)
                graphics::write_ppm(canvas_, options_.output / (key + ".ppm"));
            if (!items_.shot.empty() && ++item_shots_[items_.shot] <= 4) // settled frame of each item screen
                graphics::write_ppm(canvas_, options_.output / ("items-" + items_.shot + ".ppm"));
        }
    }

    // Full-screen new-game intro (screens/intro.hpp). Covers the board until it ends or is skipped.
    void draw_intro() {
        const auto now = SDL_GetTicks();
        const auto elapsed = intro_ticks_ ? static_cast<unsigned>(std::min<Uint64>(now - intro_ticks_, 100)) : 0;
        intro_ticks_ = now;
        if (!intro_.active())
            return;
        intro_.draw(canvas_, elapsed, [this](std::size_t sound) {
            if (sound_effects_)
                audio_->play_sound(sound);
        });
        if (!intro_.active())
            return;
        buttons_.clear(); // input is blocked while the intro plays
        if (options_.play_effects && !options_.output.empty() && now >= intro_capture_ && intro_frames_ < 400) {
            std::ostringstream name;
            name << "intro-" << std::setw(3) << std::setfill('0') << intro_frames_++ << ".ppm";
            graphics::write_ppm(canvas_, options_.output / name.str());
            intro_capture_ = now + 80;
        }
    }
    // EFFECTS: plays queued original animations (tools/screens/effects.cpp) over the board viewport.
    void draw_effects() {
        const auto now = SDL_GetTicks();
        const auto elapsed = effect_ticks_ ? static_cast<unsigned>(std::min<Uint64>(now - effect_ticks_, 100)) : 0;
        effect_ticks_ = now;
        if (!effects_.active())
            return;
        effects_.draw(canvas_, *board_, session_->world(), static_cast<std::uint8_t>(rotation_), elapsed, now,
                      [this](std::size_t sound) {
                          if (sound_effects_)
                              audio_->play_sound(sound);
                      });
        if (effects_.active())
            buttons_.clear(); // input is blocked while an effect plays, as in the original
        if (options_.play_effects && !options_.output.empty() && now >= effect_capture_ && effect_frames_ < 200) {
            graphics::write_ppm(canvas_, options_.output / ("effect-" + std::to_string(effect_frames_++) + ".ppm"));
            effect_capture_ = now + 300;
        }
    }
    unsigned spinner_elapsed() { // SPINNERS: real time between frames (replays: 40 ms per frame)
        const auto now = SDL_GetTicks();
        const auto elapsed = spinner_ticks_ ? static_cast<unsigned>(std::min<Uint64>(now - spinner_ticks_, 100)) : 0;
        spinner_ticks_ = now;
        return options_.auto_rounds ? 40U : elapsed;
    }
    void draw_board() {
        const auto &world = session_->world();
        sync_decision();
        // Follow the turn being presented, not the core's (possibly already advanced) current player.
        main_screen_.set_displayed_player(presentation_.empty() && !presenting() ? std::nullopt
                                                                                 : presented_player_);
        const auto &active = world.players[main_screen_.displayed_player(world)].state;
        std::vector<board::PlayerSprite> players;
        for (const auto &player : world.players) {
            const auto &state = player.state;
            if (!state.controller_flags || (!state.x && !state.y))
                continue;
            board::Point position{state.x, state.y};
            bool moving = false;
            if (const auto &animation = session_->animation(); animation && animation->player == state.slot) {
                const auto &movement = *animation;
                position.x = movement.from_x + (movement.to_x - movement.from_x) * int(movement.elapsed_ms) /
                                                   int(movement.duration_ms);
                position.y = movement.from_y + (movement.to_y - movement.from_y) * int(movement.elapsed_ms) /
                                                   int(movement.duration_ms);
                moving = true;
            }
            players.push_back(
                {position, state.character, state.direction,
                 static_cast<std::uint8_t>((SDL_GetTicks() / 120) % 6),
                 static_cast<std::uint8_t>(state.traffic_method & 3U),
                 static_cast<std::uint16_t>(state.color), state.slot, state.slot == active.slot, moving,
                 state.node_id < world.nodes.size() && (world.nodes[state.node_id].flags & 0x80000000U) != 0,
                 player.status.days_sleep_walking != 0, (state.controller_flags & 0x40U) != 0});
            if (const auto toss = main_screen_.toss_frame(state.slot)) { // pre-roll toss pose
                players.back().tossing = true;
                players.back().animation_frame = *toss;
            }
        }
        std::vector<board::PropertyState> lands, facilities;
        for (std::size_t index = 1; index < world.lands.size(); ++index) {
            const auto &land = world.lands[index];
            lands.push_back({land.id, land.economy.owner_code, land.economy.level, land.economy.chain_store});
        }
        for (std::size_t index = 1; index < world.facilities.size(); ++index) {
            const auto &facility = world.facilities[index];
            facilities.push_back({facility.id, facility.owner_code, facility.level, facility.facility_type});
        }
        board::Point center{active.x, active.y};
        for (const auto &player : players)
            if (player.active)
                center = player.position;
        center = main_screen_.camera_center(center);
        if (target_center_ && session_->pending_decision())
            center = *target_center_;
        if (const auto speaker = messages_.focus(); speaker && *speaker < world.players.size())
            center = {world.players[*speaker].state.x, world.players[*speaker].state.y}; // Bubble speaker.
        board::Camera camera{center, static_cast<std::uint8_t>(rotation_)};
        current_camera_ = camera;
        std::vector<board::ObjectSprite> objects;
        for (const auto &object : world.objects) {
            std::optional<board::Point> position;
            if (object.attached_player) {
                for (const auto &player : players)
                    if (player.slot + 1 == object.attached_player)
                        position = player.position;
            } else if (object.node_id && object.node_id < world.nodes.size()) {
                const auto &node = world.nodes[object.node_id];
                position = board::Point{node.x, node.y};
            }
            if (!position)
                continue;
            objects.push_back({*position, object.kind, object.direction,
                               static_cast<std::uint8_t>((SDL_GetTicks() / 120) % 6)});
        }
        std::vector<board::SpecialPlayerSprite> special_players;
        for (std::size_t index = 0; index < world.special_actors.size(); ++index) {
            const auto &actor = world.special_actors[index];
            if (actor.inactive_state || !actor.node_id || actor.node_id >= world.nodes.size())
                continue;
            board::Point position{actor.x, actor.y};
            bool moving = false;
            if (const auto &animation = session_->animation(); animation && animation->player == index + 4) {
                const auto &movement = *animation;
                position.x = movement.from_x + (movement.to_x - movement.from_x) * int(movement.elapsed_ms) /
                                                   int(movement.duration_ms);
                position.y = movement.from_y + (movement.to_y - movement.from_y) * int(movement.elapsed_ms) /
                                                   int(movement.duration_ms);
                moving = true;
            }
            special_players.push_back({position, static_cast<std::uint8_t>(index + 4), actor.direction,
                                       static_cast<std::uint8_t>((SDL_GetTicks() / 120) % 6), false, moving,
                                       (world.nodes[actor.node_id].flags & 0x80000000U) != 0,
                                       actor.days_sleep_walking != 0});
        }
        std::vector<board::PropertyState> companies;
        for (const auto &company : world.commercials)
            if (company.id)
                companies.push_back({company.id, company.owner_code, 0, 0});
        board_->render(canvas_, {players, objects, lands, facilities, special_players, companies}, camera);
        native::MainScreenActions actions;
        actions.help = [this] { notify_action(false, "說明功能尚未提供"); };
        actions.options = [this] { screen_ = Screen::settings; };
        actions.ai = [this] { screen_ = Screen::ai_settings; };
        actions.load = [this] { open_load(); };
        actions.save = [this] { open_save(); };
        actions.query = [this] { screen_ = Screen::wealth; };
        actions.tools = [this] { screen_ = Screen::tools; };
        actions.cards = [this] { screen_ = Screen::cards; };
        actions.trading = [this] {
            if (session_->open_trading()) {
                screen_ = Screen::trading;
                trading_board_.reset();
            }
        };
        actions.stocks = [this] { screen_ = Screen::stocks; };
        actions.rotate = [this](unsigned increment) { rotate(increment); };
        actions.roll = [this] { session_->roll_dice(); };
        actions.dice_count = [this](unsigned count) {
            const bool success = session_->set_dice_count(count);
            if (success)
                ++dice_selections_;
            notify_action(success);
        };
        actions.sound = [this](unsigned id) {
            if (sound_effects_)
                audio_->play_sound(id);
        };
        actions.can_save = session_->can_save();
        actions.can_load = fs::exists(load_path());
        main_screen_.draw(frame_, *session_, rotation_, actions);
        // A decision appears only after everything emitted before it (e.g. a god's entrance movie and
        // caption before its spinner) has been shown, as the original blocks until then.
        if (screen_ == Screen::game && session_->pending_decision() && !messages_.active() &&
            presentation_.empty() && !presenting())
            draw_decision();
        if (session_->phase() == GamePhase::finished) {
            buttons_.clear();
            dialog({95, 125, 545, 350}, "遊戲結束");
            const auto winner = session_->winning_player();
            const auto result =
                winner ? std::string(names.at(world.players.at(*winner).state.character)) + "獲勝！"
                       : "本局已結束";
            font_.draw(canvas_, result, 120, 190, 18, ui::ink, 395);
            button(71, {125, 280, 275, 322}, "存檔", [this] { save(); });
            button(110, {295, 280, 515, 322}, "返回主選單", [this] { screen_ = Screen::menu; });
        }
    }
    // 託管AI: the player whose turn is shown is preselected in the window.
    std::size_t presented_player() const {
        return presented_player_.value_or(session_ ? session_->world().header.current_player : 0);
    }
    screens::AiSettingsActions ai_settings_actions() {
        return {[this](std::size_t slot, const AiSettings &settings) {
                    const bool waits = session_->phase() != GamePhase::human_ready &&
                                       session_->phase() != GamePhase::finished;
                    if (session_->set_ai_settings(slot, settings) && waits)
                        notify_action(false, "設定將於本回合結束後生效");
                    observe(); // an idle hand-over starts the computer turn at once
                },
                [this] { screen_ = Screen::game; }};
    }
    native::items::Host item_host() {
        return {[this] { screen_ = Screen::game; },
                [this](bool success, std::string_view failure) { notify_action(success, failure); },
                [this](std::size_t index) { return respond(index); }};
    }
    void draw_inventory() {
        buttons_.clear();
        items_.draw_inventory(frame_, *session_, screen_ == Screen::cards, item_host());
    }
    void draw_stocks() {
        buttons_.clear();
        auto host = make_host();
        stock_market_.draw(host, session_->world(), session_->world().header.current_player, pad_, stock_actions());
    }
    void close_stocks() {
        end_amount_edit();
        pad_.close();
        stock_market_.reset();
        screen_ = Screen::game;
        notify_action(session_->close_stock_market(), "目前無法完成股市結算");
    }
    bool trade(bool buy, std::int32_t quantity) {
        end_amount_edit();
        const bool success = buy ? session_->buy_stock(selected_stock_, quantity)
                                 : session_->sell_stock(selected_stock_, quantity);
        if (success) {
            if (buy)
                ++stocks_bought_;
            else
                ++stocks_sold_;
        }
        notify_action(success, "休市、限價或可交易數量不足");
        return success;
    }
    void close_trading() {
        end_amount_edit();
        pad_.close();
        trading_board_.reset();
        screen_ = Screen::game;
        notify_action(session_->close_trading(), "目前無法完成買賣結算");
    }
    screens::TradingActions trading_actions() {
        return {[this](const trading::Listing &listing) {
                    const bool success = session_->post_offer(listing);
                    if (success)
                        ++offers_posted_;
                    notify_action(true);
                    return success;
                },
                [this](std::size_t index) {
                    const bool success = session_->cancel_offer(index);
                    if (success)
                        ++offers_cancelled_;
                    notify_action(true);
                    return success;
                },
                [this](std::size_t seller, std::size_t index) {
                    const bool success = session_->buy_offer(seller, index);
                    if (success)
                        ++offers_bought_;
                    notify_action(true);
                    return success;
                },
                [this] { close_trading(); }};
    }
    void draw_trading() {
        buttons_.clear();
        auto host = make_host();
        trading_board_.draw(host, session_->world(), session_->world().header.current_player, pad_,
                            trading_actions());
    }
    void sync_decision() {
        if (!session_->pending_decision()) {
            items_.end_visits();
            decision_key_.clear();
            selected_target_.reset();
            target_center_.reset();
            return;
        }
        const auto &decision = *session_->pending_decision();
        auto key = decision.prompt + ':' + std::to_string(static_cast<int>(decision.kind));
        for (const auto &option : decision.options)
            key += ':' + std::to_string(option.value);
        if (decision_key_ == key)
            return;
        end_amount_edit();
        pad_.close(); // a number pad never outlives the decision it was opened for
        decision_key_ = std::move(key);
        decision_page_ = 0;
        selected_target_.reset();
        target_center_.reset();
        if (decision.kind == DecisionKind::primary_stock)
            bank_amount_ = std::min(100, decision.max_amount);
    }
    bool map_decision() const {
        if (!session_->pending_decision())
            return false;
        const auto &options = session_->pending_decision()->options;
        return std::any_of(options.begin(), options.end(),
                           [](const auto &option) { return option.location.has_value(); });
    }
    void select_target(std::size_t index) {
        if (!session_ || !session_->pending_decision() ||
            index >= session_->pending_decision()->options.size())
            return;
        selected_target_ = index;
        const auto &option = session_->pending_decision()->options.at(index);
        if (option.location)
            target_center_ = board::Point{option.location->x, option.location->y};
    }
    void draw_map_decision() {
        const auto &decision = *session_->pending_decision();
        ui::fill(canvas_, {440, 0, 640, 480}, ui::paper);
        font_.draw(canvas_, decision.prompt, 449, 16, 16, ui::blue, 181);
        constexpr std::size_t page_size = 8;
        const auto first = decision_page_ * page_size;
        items_.draw_targets(frame_, decision, current_camera_, selected_target_, [this](std::size_t index) {
            if (selected_target_ != index) {
                select_target(index); // first tap selects and centres the camera
                return;
            }
            if (sound_effects_)
                audio_->play_sound(2);
            respond(index); // second tap on the same target confirms
        });
        font_.draw(canvas_, "點地圖上的目標，再點一次確認", 449, 66, 12, ui::color(144, 38, 17));
        for (std::size_t index = 0; index < decision.options.size(); ++index) {
            const auto &option = decision.options[index];
            if (index >= first && index < first + page_size) {
                const int y = 91 + int(index - first) * 34;
                button(210 + int(index), {448, y, 633, y + 30},
                       (option.location ? std::to_string(index + 1) + " " : "") + option.label,
                       [this, index] {
                           if (!session_->pending_decision() ||
                               index >= session_->pending_decision()->options.size())
                               return;
                           if (session_->pending_decision()->options[index].location)
                               select_target(index);
                           else
                               respond(index);
                       });
                if (selected_target_ == index)
                    ui::border(canvas_, {450, y + 2, 631, y + 28}, ui::blue);
            }
        }
        button(203, {448, 369, 538, 398}, "上一頁", [this] { --decision_page_; }, decision_page_ > 0);
        button(
            204, {543, 369, 633, 398}, "下一頁", [this] { ++decision_page_; },
            first + page_size < decision.options.size());
        button(
            207, {449, 420, 560, 462}, "確認目標",
            [this] {
                if (selected_target_)
                    respond(*selected_target_);
            },
            selected_target_.has_value());
        const auto cancel = std::find_if(decision.options.begin(), decision.options.end(),
                                         [](const auto &option) { return option.label == "取消"; });
        if (cancel != decision.options.end()) {
            const auto index = static_cast<std::size_t>(cancel - decision.options.begin());
            button(208, {567, 420, 632, 462}, "取消", [this, index] { respond(index); });
        }
    }
    void draw_decision() {
        buttons_.clear(); // A modal must never activate covered board controls.
        const auto &decision = *session_->pending_decision();
        if (decision.kind == DecisionKind::bank) {
            auto host = make_host();
            bank_screen_.draw(host, session_->world(), decision, pad_, bank_actions());
            return;
        }
        if (decision.kind == DecisionKind::primary_stock) {
            auto host = make_host();
            screens::draw_primary_stock(host, session_->world(), decision, pad_, bank_actions());
            return;
        }
        if (decision.kind == DecisionKind::lottery || decision.kind == DecisionKind::auction) {
            auto host = make_host();
            const auto answer = [this](std::size_t index) { respond(index); };
            if (decision.kind == DecisionKind::lottery)
                screens::draw_lottery(host, session_->world(), decision, answer);
            else
                auction_room_.draw(host, session_->world(), decision, answer);
            return;
        }
        if (map_decision()) {
            draw_map_decision();
            return;
        }
        if (items_.draw_decision(frame_, *session_, item_host()))
            return;
        if (spinners::handles(decision)) { // SPINNERS: slot machine / cupid wheel; STOP keeps id 210
            spinners_.draw_decision(frame_, session_->world(), decision, 210, [this] { respond(0); });
            return;
        }
        if (messages::is_yes_no(decision)) {
            // Original §2.2 question box + Data.mkf 440 strip; ids stay 210+option index.
            const auto yes = yes_no_index(1), no = yes_no_index(0);
            messages::draw_yes_no(frame_, messages::yes_no_question(decision, session_->world()), 210 + int(yes),
                                  [this, yes] { respond(yes); }, 210 + int(no), [this, no] { respond(no); });
            return;
        }
        constexpr std::size_t page_size = 8;
        const bool multiple = decision.options.size() > 3;
        const int top = multiple ? 52 : 115, bottom = multiple ? 438 : 360;
        dialog({72, top, 570, bottom}, "請選擇");
        font_.draw(canvas_, decision.prompt, 97, top + 50, 18, ui::ink, 448);
        if (decision.kind == DecisionKind::shop || decision.kind == DecisionKind::rescue)
            font_.draw(canvas_,
                       "可用點數 " +
                           std::to_string(session_->world().players.at(decision.player).state.points),
                       110, top + 110, 18, ui::blue);
        else if (decision.amount)
            font_.draw(canvas_,
                       decision.kind == DecisionKind::roulette ? "轉盤 " + std::to_string(decision.amount)
                                                               : ui::money(decision.amount),
                       110, top + 110, 22, ui::blue);
        const int width =
            std::min(160, 430 / static_cast<int>(std::max<std::size_t>(1, decision.options.size())));
        const auto first = decision_page_ * page_size;
        for (std::size_t index = first; index < std::min(first + page_size, decision.options.size());
             ++index) {
            const int position = int(index - first);
            const int x = multiple ? 93 + position % 2 * 232 : 105 + position * (width + 8);
            const bool bank = decision.kind == DecisionKind::bank;
            const int y = multiple ? (bank ? 200 + position / 2 * 36 : 208 + position / 2 * 44) : 290;
            button(210 + int(index), {x, y, x + (multiple ? 220 : width), y + (bank ? 32 : 36)},
                   decision.options[index].label, [this, index] { respond(index); });
        }
        if (decision.options.size() > page_size) {
            button(203, {105, 392, 290, 424}, "上一頁", [this] { --decision_page_; }, decision_page_ > 0);
            button(
                204, {335, 392, 520, 424}, "下一頁", [this] { ++decision_page_; },
                first + page_size < decision.options.size());
        }
    }
    std::size_t yes_no_index(std::int32_t value) const {
        const auto &options = session_->pending_decision()->options;
        for (std::size_t i = 0; i < options.size(); ++i)
            if (options[i].value == value)
                return i;
        return 0;
    }
    bool respond(std::size_t index) {
        const auto &decision = session_->pending_decision();
        if (!decision)
            return false;
        end_amount_edit();
        const auto amount =
            decision->kind == DecisionKind::bank || decision->kind == DecisionKind::primary_stock
                ? bank_amount_
                : 0;
        if (!session_->respond(index, amount)) {
            toast_ = "金額、點數或持有數量不足";
            toast_until_ = SDL_GetTicks() + 2200;
            return false;
        }
        return true;
    }
    // split: release on a later frame, so press-and-hold feedback is exercised and visible.
    void push_button(int id, bool split = false) {
        for (const auto &button : buttons_)
            if (button.id == id && button.enabled) {
                SDL_Event event{};
                event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                event.button.windowID = SDL_GetWindowID(window_.get());
                event.button.which = 0;
                event.button.button = SDL_BUTTON_LEFT;
                require(SDL_RenderCoordinatesToWindow(
                    renderer_.get(), float(button.rectangle.left + button.rectangle.right) / 2,
                    float(button.rectangle.top + button.rectangle.bottom) / 2, &event.button.x,
                    &event.button.y));
                require(SDL_PushEvent(&event));
                event.type = SDL_EVENT_MOUSE_BUTTON_UP;
                if (split)
                    test_release_ = event;
                else
                    require(SDL_PushEvent(&event));
                return;
            }
        throw std::runtime_error("Native UI replay cannot find enabled button " + std::to_string(id));
    }
    void drive_test() {
        if (test_stage_ == 0) {
            push_button(options_.start_new || options_.load.empty()
                            ? (selected_stage_ ? screens::ids::menu_new_stage : screens::ids::menu_start)
                            : screens::ids::menu_load);
            test_stage_ = 1;
            return;
        }
        if (screen_ == Screen::new_game) {
            if (setup_.map() != selected_map_)
                push_button(screens::ids::setup_map + int(selected_map_));
            else if (setup_.picks().empty())
                push_button(screens::ids::setup_character + int(selected_character_));
            else
                push_button(screens::ids::setup_ok);
            return;
        }
        if (screen_ == Screen::save) {
            push_button(screens::ids::save_slot + 1);
            return;
        }
        if (screen_ == Screen::load) {
            push_button(screens::ids::load_slot + 1);
            return;
        }
        if (!session_) {
            if (load_failed_)
                throw std::runtime_error("Native UI replay could not load its initial save");
            return;
        }
        if (screen_ == Screen::game && messages_.active())
            return; // An overlay is showing for exactly this frame (instant mode); controls are covered.
        if (auction_room_.sold_active() || items_.magic_cast_active())
            return; // --play-effects: the sold sequence / computer magic cast covers the controls
        if (session_->pending_decision() && (!presentation_.empty() || presenting()))
            return; // The decision is not shown until its preceding presentation has played.
        if (options_.play_effects && session_->pending_decision() && // dev strips: let the reels spin
            spinners::handles(*session_->pending_decision()) && ++test_spinner_frames_ < 30)
            return;
        test_spinner_frames_ = 0;
        if (test_cancel_pending_) {
            if (session_->phase() != GamePhase::human_ready || session_->pending_decision() ||
                session_->world().export_save() != test_cancel_save_ ||
                session_->random_state() != test_cancel_random_ || input_events_ != test_cancel_inputs_ + 1)
                throw std::runtime_error("Batched cancel did not preserve state or executed a stale control");
            test_cancel_pending_ = false;
            test_batched_cancel_ = true;
        }
        if (const auto &decision = session_->pending_decision()) {
            const auto &player = session_->world().active_player().state;
            const auto progress = decision_key_ + ':' + std::to_string(decision->amount) + ':' +
                                  std::to_string(session_->random_state()) + ':' +
                                  std::to_string(player.cash) + ':' + std::to_string(player.bank_balance) +
                                  ':' + std::to_string(player.points);
            if (progress == test_progress_)
                ++test_unchanged_frames_;
            else {
                test_progress_ = progress;
                test_unchanged_frames_ = 0;
            }
            if (test_unchanged_frames_ > 100) {
                graphics::write_ppm(canvas_, options_.output / "stalled.ppm");
                throw std::runtime_error("Native UI replay stalled at decision: " + decision->prompt);
            }
        } else {
            test_progress_.clear();
            test_unchanged_frames_ = 0;
        }
        if (pad_.active()) {
            // Type the amount on the number pad, one key per frame, then Enter.
            std::int32_t target = 1;
            if (const auto &decision = session_->pending_decision())
                target = decision->kind == DecisionKind::primary_stock ? std::min(100, decision->max_amount) : 1000;
            else if (screen_ == Screen::trading)
                target = trading_board_.suggestion();
            push_button(pad_.next_key_towards(target));
            return;
        }
        if (test_release_) {
            require(SDL_PushEvent(&*test_release_));
            test_release_.reset();
            return;
        }
        if (items_.has_deferred())
            return; // a released tile or timeout acts at the start of the next frame
        if (items_.popup_active()) {
            push_button(native::items::popup_skip_id);
            return;
        }
        if (screen_ == Screen::cards || screen_ == Screen::tools) {
            // Release on the tile uses it; if it could not be used the grid stays open, so go back.
            if (test_inventory_pushed_)
                push_button(native::items::back_id);
            else
                push_button(300 + int(test_inventory_id_), true);
            test_inventory_pushed_ = !test_inventory_pushed_;
            return;
        }
        test_inventory_pushed_ = false;
        if (screen_ == Screen::stocks) {
            if (screens::market_closed(session_->world()))
                push_button(screens::ids::stock_exit);
            else if (test_stock_stage_ == 1) {
                push_button(430);
                ++test_stock_stage_;
            } else if (test_stock_stage_ == 2) {
                push_button(431);
                ++test_stock_stage_;
            } else
                push_button(60);
            return;
        }
        if (screen_ == Screen::trading) {
            using View = screens::TradingBoard::View;
            const auto view = trading_board_.view();
            if (view == View::detail) {
                // Act once on the opened offer (撤件 / 購買), then close the card if it is still open.
                push_button(test_trade_acted_ ? screens::ids::trade_detail_exit : screens::ids::trade_act);
                test_trade_acted_ = !test_trade_acted_;
                return;
            }
            test_trade_acted_ = false;
            if (view != View::board) {
                // Post the first listable item: SALE menu category, then the item in its picker.
                const auto items = trading::listable_items(session_->world(), session_->world().header.current_player);
                if (items.empty())
                    throw std::runtime_error("Trading fixture has nothing to list");
                push_button(view == View::menu ? screens::ids::trade_category + int(items.front().kind) - 1
                                               : screens::ids::trade_item);
                return;
            }
            if (!offers_posted_) {
                push_button(480);
                return;
            }
            const auto quotes = trading::quotes(session_->world());
            const auto actor = session_->world().header.current_player;
            for (std::size_t i = 0; i < quotes.size(); ++i) {
                const bool own = quotes[i].seller == actor;
                if ((!offers_cancelled_ && own) ||
                    (offers_cancelled_ && !offers_bought_ && !own &&
                     !test_trade_attempts_.contains({quotes[i].seller, quotes[i].index}) &&
                     quotes[i].offer.price <= session_->world().active_player().state.cash)) {
                    if (std::any_of(buttons_.begin(), buttons_.end(), [i](const auto &button) {
                            return button.id == 600 + int(i) && button.enabled;
                        })) {
                        test_trade_attempts_.insert({quotes[i].seller, quotes[i].index});
                        push_button(600 + int(i));
                    }
                    return;
                }
            }
            if (!offers_cancelled_ || !offers_bought_)
                throw std::runtime_error("Trading fixture has no usable offer for its requested interaction");
            push_button(481);
            return;
        }
        if (test_stage_ == 1 && session_->can_save()) {
            if (options_.overlay_gallery)
                dump_overlay_gallery();
            ui::write_file(options_.output / "before.DAT", session_->export_save().encode());
            graphics::write_ppm(canvas_, options_.output / "before.ppm");
            test_stage_ = 2;
        }
        if (test_stage_ == 2 &&
            (session_->completed_days() >= options_.auto_rounds ||
             session_->phase() == GamePhase::finished) &&
            session_->can_save()) {
            test_completed_days_ = session_->completed_days();
            test_finished_ = session_->phase() == GamePhase::finished;
            expected_reload_ = session_->export_save().encode();
            expected_random_ = session_->random_state();
            ui::write_file(options_.output / "after.DAT", expected_reload_);
            graphics::write_ppm(canvas_, options_.output / "after.ppm");
            push_button(71);
            test_stage_ = 3;
            return;
        }
        if (test_stage_ == 3) {
            test_load_count_before_reload_ = loads_;
            if (test_finished_) {
                push_button(110);
                test_stage_ = 5;
            } else {
                push_button(72);
                test_stage_ = 4;
            }
            return;
        }
        if (test_stage_ == 5) {
            push_button(2);
            test_stage_ = 4;
            return;
        }
        if (test_stage_ == 4) {
            if (load_failed_ || loads_ != test_load_count_before_reload_ + 1)
                throw std::runtime_error("Final reload failed to install a newly loaded game");
            if (!session_->can_save())
                throw std::runtime_error("Reload did not restore a playable human turn");
            reload_identical_ = session_->export_save().encode() == expected_reload_;
            reload_random_identical_ = session_->random_state() == expected_random_;
            finish_test();
            return;
        }
        if (test_invalid_load_stage_ == 1 || test_invalid_load_stage_ == 3) {
            if (!load_failed_ || session_->export_save().encode() != test_recovery_save_ ||
                session_->random_state() != test_recovery_random_)
                throw std::runtime_error("Invalid-save recovery changed or lost the active game");
            options_.load = test_recovery_path_;
            ++test_invalid_load_stage_;
        }
        if (options_.exercise_recovery && test_invalid_load_stage_ < 4 &&
            session_->phase() == GamePhase::human_ready) {
            test_recovery_save_ = session_->export_save().encode();
            test_recovery_random_ = session_->random_state();
            test_recovery_path_ = options_.load;
            options_.load = options_.output / "deliberately-invalid.DAT";
            if (test_invalid_load_stage_ == 0)
                ui::write_file(options_.load, {1, 2, 3});
            else {
                auto invalid = test_recovery_save_;
                invalid.at(0x23) = 255; // Structurally valid DAT, invalid player character.
                ui::write_file(options_.load, invalid);
            }
            ++test_invalid_load_stage_;
            push_button(72);
            return;
        }
        if (options_.exercise_recovery && !test_batched_cancel_ && map_decision()) {
            const auto marker = std::find_if(buttons_.begin(), buttons_.end(), [](const auto &button) {
                return button.id >= 5000 && button.enabled;
            });
            const auto cancel = std::find_if(buttons_.begin(), buttons_.end(), [](const auto &button) {
                return button.id == 208 && button.enabled;
            });
            if (marker != buttons_.end() && cancel != buttons_.end()) {
                const auto marker_id = marker->id;
                test_cancel_save_ = session_->world().export_save();
                test_cancel_random_ = session_->random_state();
                test_cancel_inputs_ = input_events_;
                push_button(208);
                push_button(marker_id);
                test_cancel_pending_ = true;
                tested_cards_ = tested_tools_ = false;
                return;
            }
        }
        if (session_->phase() == GamePhase::human_ready) {
            const auto &player = session_->world().active_player().state;
            if (options_.exercise_trading && (!offers_posted_ || !offers_cancelled_ || !offers_bought_)) {
                push_button(80);
                return;
            }
            if (test_stock_stage_ == 0) {
                stock_market_.selected = 7;
                test_stock_stage_ = 1;
                push_button(79);
                return;
            }
            if (!tested_cards_) {
                tested_cards_ = true;
                std::vector<unsigned> available;
                for (const auto id : player.cards)
                    if (active_card(id))
                        available.push_back(id);
                if (!available.empty()) {
                    test_inventory_id_ =
                        available[(options_.seed + session_->completed_days()) % available.size()];
                    if (options_.exercise_card && !card_ids_used_.contains(options_.exercise_card) &&
                        std::find(available.begin(), available.end(), options_.exercise_card) !=
                            available.end())
                        test_inventory_id_ = options_.exercise_card;
                    push_button(77);
                    return;
                }
            }
            if (!tested_tools_) {
                tested_tools_ = true;
                std::vector<unsigned> available;
                for (unsigned i = 0; i < services::tool_names.size(); ++i)
                    if (player.tool_amounts[i])
                        available.push_back(i + 1);
                if (!available.empty()) {
                    test_inventory_id_ =
                        available[(options_.seed + session_->completed_days()) % available.size()];
                    if (options_.exercise_tool && !tool_ids_used_.contains(options_.exercise_tool) &&
                        std::find(available.begin(), available.end(), options_.exercise_tool) !=
                            available.end())
                        test_inventory_id_ = options_.exercise_tool;
                    push_button(78);
                    return;
                }
            }
            if (!tested_dice_) {
                tested_dice_ = true;
                if (std::any_of(buttons_.begin(), buttons_.end(),
                                [](const auto &button) { return button.id == 101 && button.enabled; })) {
                    push_button(101);
                    return;
                }
            }
            push_button(100);
        } else if (const auto &decision = session_->pending_decision()) {
            std::size_t choice = 0;
            if (decision->kind == DecisionKind::bank) {
                // ATM: deposit 1000 once per visit by typing on the ATM keys, then the loan desk's EXIT.
                const auto &player = session_->world().active_player().state;
                if (bank_screen_.at_atm()) {
                    if (bank_done_for_visit_ || player.cash < 1000) {
                        bank_done_for_visit_ = true;
                        push_button(screens::ids::atm_exit);
                    } else if (bank_screen_.atm_mode() != 1)
                        push_button(screens::ids::atm_deposit);
                    else if (const auto &digits = bank_screen_.atm_digits(); digits == "1000") {
                        bank_done_for_visit_ = true;
                        push_button(screens::ids::atm_enter);
                    } else
                        push_button(digits == "0"                      ? screens::ids::atm_digit(1)
                                    : std::string("1000").starts_with(digits) ? screens::ids::atm_digit(0)
                                                                              : screens::ids::atm_clear);
                } else
                    push_button(bank_screen_.at_director() ? screens::ids::director_exit : screens::ids::desk_exit);
                return;
            } else if (decision->kind == DecisionKind::shop) {
                const auto &player = session_->world().players.at(decision->player).state;
                for (std::size_t i = 0; i < decision->options.size(); ++i)
                    if (!decision->options[i].value)
                        choice = i;
                if (!shop_done_for_visit_) {
                    for (std::size_t i = 0; i < decision->options.size(); ++i) {
                        const auto action = decision->options[i].value;
                        if ((action > 100 && action <= 130 &&
                             player.points >= services::card_prices[action - 101] &&
                             card_count(player.cards) < 15) ||
                            (action > 200 && action <= 208 &&
                             player.points >= services::tool_prices[action - 201] &&
                             player.tool_amounts[action - 201] < 9)) {
                            choice = i;
                            break;
                        }
                    }
                    shop_done_for_visit_ = true;
                }
                if (const auto pending = items_.shop_pending_value()) // confirm the item tapped once
                    for (std::size_t i = 0; i < decision->options.size(); ++i)
                        if (decision->options[i].value == *pending) {
                            choice = i; // duplicate shelf rows all answer with the first option
                            break;
                        }
            } else if (decision->kind == DecisionKind::magic_effect) {
                const auto desired = static_cast<int>((options_.seed + session_->completed_days()) % 12);
                for (std::size_t i = 0; i < decision->options.size(); ++i)
                    if (decision->options[i].value == desired)
                        choice = i;
            } else if (decision->kind == DecisionKind::rescue) {
                const auto points = session_->world().players.at(decision->player).state.points;
                for (std::size_t i = 0; i < decision->options.size(); ++i) {
                    const auto target = decision->options[i].value;
                    if (target < 0 || points >= (target < 4 ? 30 : 300)) {
                        choice = i;
                        break;
                    }
                }
            }
            if (decision_page_ != choice / 8) {
                decision_page_ = choice / 8;
                return;
            }
            if (decision->kind == DecisionKind::shop && choice < decision->options.size() &&
                items_.shop_show_option(decision->options[choice].value))
                return; // the card or tool shelf must be on screen first
            if (map_decision() && selected_target_ == choice)
                push_button(207);
            else {
                const auto marker = 5000 + int(choice);
                const bool visible_marker =
                    map_decision() &&
                    std::any_of(buttons_.begin(), buttons_.end(), [marker](const auto &button) {
                        return button.id == marker && button.enabled;
                    });
                push_button(visible_marker ? marker : 210 + int(choice));
            }
        } else if (session_->phase() == GamePhase::finished)
            throw std::runtime_error("Match ended before the requested replay duration");
    }
    // Dev tool (--gallery DIR): renders each original-style menu/finance screen, including
    // decisions that only occur mid-game (bank, primary shares), for visual review.
    bool press(int id, const std::function<void()> &redraw = {}) {
        if (redraw)
            redraw();
        else
            draw();
        for (const auto &button : buttons_)
            if (button.id == id && button.enabled) {
                auto action = button.action;
                buttons_.clear();
                action();
                return true;
            }
        return false;
    }
    int gallery() {
        fs::create_directories(options_.gallery);
        options_.save_directory = options_.gallery / "saves";
        fs::create_directories(options_.save_directory);
        const auto shot = [this](const std::string &name) {
            draw();
            graphics::write_ppm(canvas_, options_.gallery / (name + ".ppm"));
        };
        const auto point = [this](int x, int y, bool down = false) {
            frame_.pointer_x = x;
            frame_.pointer_y = y;
            frame_.pointer_down = down;
        };
        point(190, 380);
        shot("menu");
        point(-1, -1);
        press(screens::ids::menu_start);
        shot("new-game");
        press(screens::ids::setup_character + 10);
        press(screens::ids::setup_character + 3);
        point(200, 50);
        shot("new-game-picked");
        point(-1, -1);
        press(screens::ids::setup_dropdown + 1);
        point(580, 336);
        shot("new-game-dropdown");
        point(-1, -1);
        press(screens::ids::setup_dropdown_close);
        press(screens::ids::setup_ok);
        if (!session_)
            throw std::runtime_error("Gallery could not start a game");
        shot("board");
        write_save(slot_path(0));
        write_save(slot_path(2));
        screen_ = Screen::settings;
        shot("options-in-game");
        open_save();
        point(300, 200);
        shot("save");
        point(-1, -1);
        screen_ = Screen::game;
        draw();
        open_load();
        point(300, 100);
        shot("load");
        point(-1, -1);
        screen_ = Screen::wealth;
        shot("info-assets");
        press(screens::ids::info_page + 1);
        shot("info-properties");
        press(screens::ids::info_page + 2);
        shot("info-stocks");
        screen_ = Screen::stocks;
        stock_market_.selected = 3;
        shot("stocks-prices");
        press(screens::ids::stock_page);
        shot("stocks-holdings");
        press(screens::ids::stock_page);
        stock_market_.info_open = true;
        shot("stocks-info");
        stock_market_.info_open = false;
        draw();
        if (press(screens::ids::stock_buy)) {
            press(screens::ids::pad_digit(5));
            shot("stocks-pad");
        }
        pad_.close();
        stock_market_.reset();
        screen_ = Screen::game;
        // Trading board: post a tool through SALE -> picker -> price pad, then open it.
        if (session_->open_trading()) {
            screen_ = Screen::trading;
            shot("trading-board-empty");
            press(screens::ids::trade_sale);
            point(500, 140);
            shot("trading-menu");
            point(-1, -1);
            press(screens::ids::trade_category + 2);
            shot("trading-tools");
            const auto items = trading::listable_items(session_->world(), session_->world().header.current_player);
            for (std::size_t i = 0; i < items.size(); ++i)
                if (items[i].kind == OfferKind::tool) {
                    press(screens::ids::trade_item + int(i));
                    break;
                }
            for (const char digit : std::to_string(trading_board_.suggestion()))
                press(screens::ids::pad_digit(digit - '0'));
            shot("trading-price-pad");
            press(screens::ids::pad_enter);
            shot("trading-board");
            press(screens::ids::trade_offer);
            shot("trading-detail");
            press(screens::ids::trade_detail_exit);
            press(screens::ids::trade_sale);
            press(screens::ids::trade_category + 1);
            shot("trading-properties");
            close_trading();
        }
        // Synthetic decisions: the real session never pauses here on demand.
        const auto slot = session_->world().header.current_player;
        const auto render = [&](const World &world, const GameDecision &decision) {
            buttons_.clear();
            canvas_ = graphics::make_canvas(640, 480, ui::color(7, 30, 55));
            frame_.ticks = SDL_GetTicks();
            draw_board();
            buttons_.clear();
            auto host = make_host();
            const screens::BankActions none{[](std::int32_t, std::int32_t) { return false; }};
            if (decision.kind == DecisionKind::bank)
                bank_screen_.draw(host, world, decision, pad_, none);
            else if (decision.kind == DecisionKind::lottery)
                screens::draw_lottery(host, world, decision, [](std::size_t) {});
            else if (decision.kind == DecisionKind::auction)
                auction_room_.draw(host, world, decision, [](std::size_t) {});
            else
                screens::draw_primary_stock(host, world, decision, pad_, none);
            if (pad_.active()) {
                buttons_.clear();
                pad_.draw(host);
            }
        };
        const auto decision_shot = [&](const World &world, const GameDecision &decision, const std::string &name) {
            render(world, decision);
            graphics::write_ppm(canvas_, options_.gallery / (name + ".ppm"));
        };
        GameDecision bank{DecisionKind::bank, slot, 0, 0, "", {{"存款", 1}, {"提款", 2}, {"贷款", 3}, {"离开", 0}}};
        const auto &world = session_->world();
        const std::function<void()> redraw_bank = [&] { render(session_->world(), bank); };
        decision_shot(world, bank, "bank-atm");
        press(screens::ids::atm_deposit, redraw_bank);
        press(screens::ids::atm_digit(3), redraw_bank);
        press(screens::ids::atm_digit(0), redraw_bank);
        press(screens::ids::atm_digit(0), redraw_bank);
        press(screens::ids::atm_digit(0), redraw_bank);
        point(260, 343, true); // holding MAX
        decision_shot(world, bank, "bank-atm-typed");
        point(-1, -1);
        press(screens::ids::atm_exit, redraw_bank);
        decision_shot(world, bank, "bank-desk");
        press(screens::ids::desk_loan, redraw_bank);
        press(screens::ids::pad_digit(2), redraw_bank);
        press(screens::ids::pad_digit(0), redraw_bank);
        decision_shot(world, bank, "bank-desk-pad");
        pad_.close();
        // Director's office: make the active player the owner of a bank building.
        World owner = world;
        auto &player = owner.players.at(slot).state;
        for (std::size_t node = 1; node < owner.nodes.size(); ++node) {
            const auto code = owner.nodes[node].type_and_index;
            if (code > 6000 && code < 8000 && std::size_t(code - 6000) < owner.commercials.size()) {
                player.node_id = static_cast<std::uint16_t>(node);
                owner.commercials[code - 6000].owner_code = static_cast<std::uint8_t>(slot + 1);
                break;
            }
        }
        player.loan = 50000;
        owner.players.at(slot).status.loan_due_date = calendar::pack(calendar::add_days(
            {owner.header.date.year, owner.header.date.month, owner.header.date.day}, 40));
        bank.options = {{"存款", 1}, {"提款", 2}, {"贷款", 3}, {"还贷", 4}, {"银行融资", 5}, {"离开", 0}};
        bank_screen_.reset();
        press(screens::ids::atm_exit, [&] { render(owner, bank); });
        const std::function<void()> redraw_owner = [&] { render(owner, bank); };
        decision_shot(owner, bank, "bank-desk-owner");
        press(screens::ids::desk_window, redraw_owner);
        decision_shot(owner, bank, "bank-director");
        bank_screen_.reset();
        GameDecision primary{DecisionKind::primary_stock, slot, 1, 52, "", {{"认购", 1}, {"放弃", 0}}, 380};
        decision_shot(world, primary, "primary-stock");
        pad_.close();
        GameDecision lottery{DecisionKind::lottery, slot, 0, 1000, "", {}};
        for (std::int32_t n = 1; n <= 36; ++n)
            if (n % 7 != 3)
                lottery.options.push_back({std::to_string(n), n});
        lottery.options.push_back({"离开", 0});
        point(30 + 64 * 4 + 20, 271 + 48 + 20);
        decision_shot(world, lottery, "lottery");
        point(-1, -1);
        GameDecision auction{DecisionKind::auction, slot, 2001, 2500, "", {}};
        for (std::int32_t action = 0; action <= 6; ++action)
            auction.options.push_back({"", action});
        point(406, 229, true);
        decision_shot(world, auction, "auction");
        point(-1, -1);
        // AUCTION poses: one bidder cannot bid (28+3c); the human's +1000 bid animating (27+3c, starburst).
        auction.inactive_players = static_cast<std::uint8_t>(1U << ((slot + 1) % world.players.size()));
        auction_room_.preview_bid(auction, 3, 3, auction.amount + 1000, SDL_GetTicks() - 450);
        decision_shot(world, auction, "auction-bid");
        auction_room_.reset();
        for (const unsigned at : {300U, 2500U}) { // sold sequence: gavel + price, then congratulations
            auction_room_.start_sold((slot + 2) % world.players.size(), 3500, 2001);
            buttons_.clear();
            canvas_ = graphics::make_canvas(640, 480, ui::color(7, 30, 55));
            frame_.ticks = SDL_GetTicks();
            draw_board();
            auto host = make_host();
            auction_room_.draw_sold(host, world); // stamps the start
            frame_.ticks += at;
            auction_room_.draw_sold(host, world);
            graphics::write_ppm(canvas_, options_.gallery / ("auction-sold-" + std::to_string(at) + ".ppm"));
            auction_room_.reset();
        }
        // 託管AI window: the human selected with 託管 on, cards on, 大老奸, 70% cash / 30% stock.
        if (ai_settings_.open(*session_, slot) && ai_settings_.edited(slot)) {
            auto edited = *ai_settings_.edited(slot);
            screen_ = Screen::ai_settings;
            last_screen_ = Screen::ai_settings;
            press(screens::ids::ai_tab + int(slot)); // the selected tab toggles 託管
            press(screens::ids::ai_tools);
            press(screens::ids::ai_personality + 2);
            for (int i = edited.cash_ratio; i < 70; i += 10)
                press(screens::ids::ai_cash_up);
            for (int i = edited.stock_ratio; i < 30; i += 10)
                press(screens::ids::ai_stock_up);
            shot("ai-settings");
            point(499, 187, true); // holding 確定
            shot("ai-settings-pressed");
            point(-1, -1);
            screen_ = Screen::game;
        }
        session_.reset();
        screen_ = Screen::settings;
        shot("options-menu");
        std::cout << "Gallery written to " << options_.gallery << '\n';
        return 0;
    }
    // Renders sample overlays over the current board for visual review (headless --output only).
    void dump_overlay_gallery() {
        const auto &world = session_->world();
        const auto shoot = [this](const std::string &name, std::optional<messages::Item> item) {
            messages_.clear();
            if (item)
                messages_.push(std::move(*item));
            gallery_ = true;
            draw();
            gallery_ = false;
            graphics::write_ppm(canvas_, options_.output / ("gallery-" + name + ".ppm"));
            messages_.clear();
        };
        const auto event = [&](Announcement kind, std::uint8_t id = 0) {
            GameEvent sample{GameEventKind::notice, 0, 0, 0, {}};
            sample.announcement = kind;
            sample.announcement_id = id;
            return sample;
        };
        shoot("bubble-danny", messages::speech_bubble(10, 0, 0));
        shoot("bubble-john-3lines", messages::speech_bubble(0, 24, 0));
        shoot("bubble-sara-mood1", messages::speech_bubble(5, 18, 0));
        shoot("bubble-beibei-pictogram", messages::speech_bubble(11, 3, 0));
        // Card (slot row*30 + card-1) and tool lines with their call-site moods.
        shoot("bubble-card-monster-user", messages::item_speech_bubble(0, SpeechTable::card, 10, 0, 0));
        shoot("bubble-card-monster-owner", messages::item_speech_bubble(3, SpeechTable::card, 70, 1, 0));
        shoot("bubble-card-beibei-demolish", messages::item_speech_bubble(11, SpeechTable::card, 11, 0, 0));
        shoot("bubble-tool-missile", messages::item_speech_bubble(6, SpeechTable::tool, 6, 0, 0));
        shoot("bubble-tool-mine-hit", messages::item_speech_bubble(9, SpeechTable::tool, 15, 1, 0));
        std::uint16_t land = 1;
        while (land + 1U < world.lands.size() && world.lands[land].economy.street_name.empty())
            ++land;
        auto toll = event(Announcement::toll);
        toll.property = static_cast<std::uint16_t>(2000 + land);
        toll.subject = 1;
        toll.amount = 1200;
        shoot("box-toll", messages::from_event(toll, world));
        toll.second_subject = 2;
        shoot("box-toll-allied", messages::from_event(toll, world));
        shoot("box-cash", messages::from_event(event(Announcement::cash_insufficient), world));
        auto coupon = event(Announcement::point_coupon);
        coupon.amount = 50;
        shoot("box-coupon", messages::from_event(coupon, world));
        auto absent = event(Announcement::absent_status, 2);
        absent.value = 3;
        shoot("box-prison", messages::from_event(absent, world));
        for (const std::uint8_t id : {1, 5, 11, 29, 33}) {
            auto news = event(Announcement::news, id);
            news.property = static_cast<std::uint16_t>(2000 + land);
            news.players = 0b1011;
            news.player_amounts = {2940, 4185, 0, 6205};
            news.subject = 1;
            news.company = 1;
            news.value = 3;
            shoot("news-" + std::to_string(id), messages::from_event(news, world));
        }
        for (const std::uint8_t id : {0, 20, 33}) {
            auto fortune = event(Announcement::fortune, id);
            fortune.value = id == 20 ? 1000 : 3;
            shoot("fortune-" + std::to_string(id), messages::from_event(fortune, world));
        }
        shoot("god-1", messages::from_event(event(Announcement::god_arrival, 1), world));
        shoot("god-12", messages::from_event(event(Announcement::god_arrival, 12), world));
        GameDecision buy{DecisionKind::buy_land, 0, land, 800, {}, {{"购买", 1}, {"放弃", 0}}};
        for (const int hover : {0, 1, 2}) {
            messages_.clear();
            gallery_ = true;
            draw();
            gallery_ = false;
            buttons_.clear();
            frame_.pointer_x = hover == 1 ? 190 : hover == 2 ? 250 : -1;
            frame_.pointer_y = hover ? 320 : -1;
            messages::draw_yes_no(frame_, messages::yes_no_question(buy, world), 210, [] {}, 211, [] {});
            frame_.pointer_x = frame_.pointer_y = -1;
            graphics::write_ppm(canvas_, options_.output / ("gallery-yesno-" + std::to_string(hover) + ".ppm"));
        }
        buttons_.clear();
        draw();
    }
    void finish_test() {
        const bool passed =
            reload_identical_ && reload_random_identical_ &&
            (test_completed_days_ >= options_.auto_rounds || test_finished_) &&
            (!options_.exercise_card || card_ids_used_.contains(options_.exercise_card)) &&
            (!options_.exercise_tool || tool_ids_used_.contains(options_.exercise_tool)) &&
            (!options_.exercise_recovery || (test_invalid_load_stage_ == 4 && test_batched_cancel_)) &&
            (!options_.exercise_trading || (offers_posted_ && offers_cancelled_ && offers_bought_)) &&
            dice_by_player_[0] > 0 && dice_by_player_[1] + dice_by_player_[2] + dice_by_player_[3] > 0 &&
            event_counts_[GameEventKind::moved] > 0 && saves_ && loads_ && input_events_ &&
            unsupported_.empty();
        std::ofstream report(options_.output / "report.json");
        report << "{\"schema\":1,\"target\":\"native\",\"status\":\"" << (passed ? "passed" : "failed")
               << "\",\"native_gameplay_verified\":" << (passed ? "true" : "false")
               << ",\"wine_runtime\":false,\"desktop_input\":false,\"video_driver\":"
               << quoted(SDL_GetCurrentVideoDriver())
               << ",\"renderer\":" << quoted(SDL_GetRendererName(renderer_.get()))
               << ",\"requested_days\":" << options_.auto_rounds
               << ",\"completed_days\":" << test_completed_days_
               << ",\"match_finished\":" << (test_finished_ ? "true" : "false")
               << ",\"seed\":" << options_.seed
               << ",\"initial_game_random_state\":" << initial_random_state_.value_or(0)
               << ",\"map\":" << session_->world().header.stage_id * 4 + session_->world().header.map_id
               << ",\"requested_card\":" << options_.exercise_card
               << ",\"requested_tool\":" << options_.exercise_tool
               << ",\"invalid_load_recovered\":" << (test_invalid_load_stage_ == 4 ? "true" : "false")
               << ",\"offers_posted\":" << offers_posted_ << ",\"offers_cancelled\":" << offers_cancelled_
               << ",\"offers_bought\":" << offers_bought_
               << ",\"batched_cancel_recovered\":" << (test_batched_cancel_ ? "true" : "false")
               << ",\"new_games\":" << new_games_ << ",\"loads\":" << loads_ << ",\"saves\":" << saves_
               << ",\"reload_bytes_identical\":" << (reload_identical_ ? "true" : "false")
               << ",\"reload_random_identical\":" << (reload_random_identical_ ? "true" : "false")
               << ",\"sdl_input_events\":" << input_events_ << ",\"rendered_frames\":" << rendered_frames_
               << ",\"dice_actions\":" << event_counts_[GameEventKind::dice_rolled]
               << ",\"movement_steps\":" << event_counts_[GameEventKind::moved]
               << ",\"land_purchases\":" << event_counts_[GameEventKind::land_purchased]
               << ",\"land_upgrades\":" << event_counts_[GameEventKind::land_upgraded]
               << ",\"rent_payments\":" << event_counts_[GameEventKind::rent_paid]
               << ",\"bank_transfers\":" << event_counts_[GameEventKind::bank_transfer]
               << ",\"cards_used\":" << cards_used_ << ",\"tools_used\":" << tools_used_
               << ",\"stock_buys\":" << stocks_bought_ << ",\"stock_sales\":" << stocks_sold_
               << ",\"dice_selections\":" << dice_selections_ << ",\"audio_driver\":"
               << quoted(SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "")
               << ",\"audio_frames\":" << audio_->statistics().rendered_frames
               << ",\"audio_effects\":" << audio_->statistics().effects_started
               << ",\"audio_voices\":" << audio_->statistics().voices_started
               << ",\"music_available\":" << (audio_->music_available() ? "true" : "false")
               << ",\"unsupported_events\":[";
        for (std::size_t index = 0; index < unsupported_.size(); ++index) {
            if (index)
                report << ',';
            report << quoted(unsupported_[index]);
        }
        report << "],\"card_ids_used\":[";
        bool comma = false;
        for (auto id : card_ids_used_) {
            if (comma)
                report << ',';
            report << id;
            comma = true;
        }
        report << "],\"tool_ids_used\":[";
        comma = false;
        for (auto id : tool_ids_used_) {
            if (comma)
                report << ',';
            report << id;
            comma = true;
        }
        report << "]}\n";
        std::cout << "Native gameplay report: " << options_.output / "report.json" << '\n';
        running_ = false;
        final_status_ = passed ? 0 : 1;
    }

    Options options_;
    Artwork artwork_;
    ui::Font font_;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window_{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer_{nullptr, SDL_DestroyRenderer};
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture_{nullptr, SDL_DestroyTexture};
    std::unique_ptr<GameSession> session_;
    std::unique_ptr<board::Renderer> board_;
    std::unique_ptr<audio::Audio> audio_;
    native::EffectPlayer effects_{artwork_}; // EFFECTS
    native::IntroPlayer intro_{artwork_};    // new-game parachute intro (screens/intro.hpp)
    Uint64 intro_ticks_ = 0, intro_capture_ = 0;
    unsigned intro_frames_ = 0;
    bool intro_pressed_ = false; // a press began during the intro; its release skips
    spinners::Spinners spinners_{{[this](std::size_t id) { // SPINNERS
                                      if (sound_effects_)
                                          audio_->play_sound(id);
                                  },
                                  [this](std::size_t id) {
                                      if (sound_effects_)
                                          audio_->play_loop(id);
                                  },
                                  [this] { audio_->stop_loop(); }}};
    Uint64 spinner_ticks_ = 0; // SPINNERS: last spinner effect draw time
    unsigned test_spinner_frames_ = 0; // SPINNERS (--play-effects): frames shown before the replay STOP
    std::deque<PresentationStep> presentation_; // see observe()
    std::optional<std::size_t> presented_player_;
    Uint64 effect_ticks_ = 0;                // EFFECTS: last draw_effects() time
    Uint64 effect_capture_ = 0;              // EFFECTS (--play-effects): next frame dump time
    unsigned effect_frames_ = 0;
    messages::Overlay messages_{{[this](std::size_t entry) { return sound_effects_ ? audio_->play_speech(entry) : 0U; },
                                 [this] { audio_->stop_voice(); }}};
    std::map<std::string, unsigned> overlay_captures_;
    bool gallery_ = false;
    graphics::Image canvas_;
    std::vector<Button> buttons_;
    native::Frame frame_{canvas_, artwork_, font_, buttons_};
    native::MainScreen main_screen_{options_.ui_preset};
    Screen screen_ = Screen::menu, last_screen_ = Screen::menu, previous_screen_ = Screen::menu;
    screens::NumberPad pad_;
    screens::BankScreen bank_screen_;
    screens::StockMarket stock_market_;
    screens::MainMenu main_menu_;
    screens::NewGameSetup setup_;
    screens::SaveLoadScreen slots_;
    screens::OptionsScreen options_screen_;
    screens::PlayerInfo info_;
    screens::TradingBoard trading_board_;
    screens::AuctionRoom auction_room_;     // AUCTION: bid poses and the sold sequence
    screens::AiSettingsScreen ai_settings_; // 託管AI window (toolbar icon 2)
    native::items::Screens items_;
    unsigned selected_character_ = 10, selected_map_ = 0, selected_stage_ = 0, rotation_ = 0, speed_ = 1;
    bool running_ = true, bank_done_for_visit_ = false, shop_done_for_visit_ = false,
         sound_effects_ = true;
    std::int32_t bank_amount_ = 1000;
    std::int32_t amount_limit_ = 0;
    std::int32_t *editing_amount_ = nullptr;
    std::string amount_text_;
    std::size_t selected_stock_ = 0;
    std::uint64_t offers_posted_ = 0, offers_cancelled_ = 0, offers_bought_ = 0;
    std::set<std::pair<std::size_t, std::size_t>> test_trade_attempts_;
    bool test_trade_acted_ = false;
    std::optional<std::uint32_t> initial_random_state_;
    board::Camera current_camera_{};
    std::optional<board::Point> target_center_;
    std::optional<std::size_t> selected_target_;
    std::uint64_t last_autosaved_day_ = UINT64_MAX;
    std::vector<std::string> recent_, unsupported_;
    std::string toast_;
    Uint64 toast_until_ = 0;
    std::string decision_key_;
    std::size_t decision_page_ = 0;
    std::ofstream events_;
    std::ofstream inputs_;
    std::set<std::string> captured_frames_;
    std::map<std::string, unsigned> item_shots_;
    std::map<GameEventKind, std::uint64_t> event_counts_;
    std::array<std::uint64_t, 4> dice_by_player_{};
    std::uint64_t input_events_ = 0, rendered_frames_ = 0, new_games_ = 0, loads_ = 0, saves_ = 0;
    std::uint64_t cards_used_ = 0, tools_used_ = 0, stocks_bought_ = 0, stocks_sold_ = 0;
    std::uint64_t dice_selections_ = 0;
    std::set<unsigned> card_ids_used_, tool_ids_used_;
    unsigned test_inventory_id_ = 0, test_stock_stage_ = 0;
    bool tested_cards_ = false, tested_tools_ = false, tested_dice_ = false, test_inventory_pushed_ = false;
    std::string test_progress_;
    unsigned test_unchanged_frames_ = 0;
    bool load_failed_ = false, test_batched_cancel_ = false;
    bool test_cancel_pending_ = false;
    std::optional<SDL_Event> test_release_;
    std::vector<std::uint8_t> test_cancel_save_;
    std::uint32_t test_cancel_random_ = 0;
    std::uint64_t test_cancel_inputs_ = 0;
    unsigned test_invalid_load_stage_ = 0;
    fs::path test_recovery_path_;
    std::vector<std::uint8_t> test_recovery_save_;
    std::uint32_t test_recovery_random_ = 0;
    unsigned test_stage_ = 0;
    std::uint64_t test_load_count_before_reload_ = 0;
    std::uint64_t test_completed_days_ = 0;
    std::vector<std::uint8_t> expected_reload_;
    bool reload_identical_ = false;
    bool reload_random_identical_ = false;
    std::uint32_t expected_random_ = 0;
    bool test_finished_ = false;
    int final_status_ = 0;
};
} // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "rich4-native [--assets DIR] [--font FONT] [--load SAVE.DAT] [--save-dir DIR]\n"
                     "  --headless --auto-rounds N --output DIR [--start-new] [--seed N] [--overlay-gallery]\n";
        return 0;
    }
    try {
        Application application(arguments(argc, argv));
        try {
            return application.run();
        } catch (const std::exception &error) {
            try {
                application.write_diagnostic(error.what());
            } catch (const std::exception &diagnostic) {
                std::cerr << "Failure snapshot: " << diagnostic.what() << '\n';
            }
            throw;
        }
    } catch (const std::exception &error) {
        std::cerr << "rich4-native: " << error.what() << '\n';
        return 1;
    }
}
