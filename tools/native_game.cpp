// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_audio.hpp"
#include "native_frame.hpp"
#include "native_stock_names.hpp"
#include "native_ui.hpp"
#include "rich4/assets.hpp"
#include "screens/messages.hpp"
#include "rich4/board_renderer.hpp"
#include "rich4/game.hpp"
#include "rich4/services.hpp"
#include "rich4/stocks.hpp"
#include "rich4/trading.hpp"
#include "rich4/world.hpp"
#include "rich4/world_validation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __ANDROID__
#include "rich4_android.hpp" // android/app/src/main/cpp
#endif

#include <array>
#include <bit>
#include <chrono>
#include <ctime>
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
using native::Artwork;
using native::Button;
using native::map_names;
using native::names;

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
    bool exercise_trading = false;
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
#endif
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--headless")
            options.headless = true;
        else if (argument == "--hidden-window")
            options.hidden_window = true;
        else if (argument == "--exercise-recovery")
            options.exercise_recovery = true;
        else if (argument == "--exercise-trading")
            options.exercise_trading = true;
        else if (argument == "--start-new")
            options.start_new = true;
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
            else if (argument == "--exercise-tool")
                options.exercise_tool = static_cast<unsigned>(std::stoul(value));
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
    if (options.headless && options.output.empty())
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

enum class Screen { menu, new_game, game, settings, wealth, cards, tools, stocks, trading };

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
        messages_.set_instant(options_.auto_rounds != 0); // Headless replay never waits on a bubble.
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
        auto last_tick = SDL_GetTicks();
        for (unsigned iteration = 0; running_; ++iteration) {
            draw();
            if (options_.auto_rounds)
                drive_test();
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
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
                            if (sound_effects_)
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
                        else if (session_->pending_decision()) {
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
                    if (event.key.key == SDLK_F5 && session_ && session_->can_save())
                        save();
                    if (event.key.key == SDLK_F9 && fs::exists(load_path()))
                        load(load_path());
                }
            }
            const auto now = SDL_GetTicks();
            const unsigned elapsed =
                options_.auto_rounds ? 500
                                     : static_cast<unsigned>(std::min<Uint64>(now - last_tick, 100)) * speed_;
            last_tick = now;
            if (session_ && screen_ == Screen::game) {
                if (!messages_.active()) // The original game loop is blocked by every popup.
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
        if (session_ && session_->can_save() && !options_.auto_rounds)
            save();
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
    void amount_control(std::int32_t &amount, std::int32_t maximum, std::int32_t step, int y,
                        bool show_all = true, int first_id = 201) {
        const auto limit = std::max<std::int32_t>(0, maximum);
        amount = std::clamp(amount, 0, limit);
        button(first_id, {103, y, 151, y + 30}, "-", [this, &amount, step] {
            end_amount_edit();
            amount = std::max<std::int32_t>(0, amount - step);
        });
        button(first_id + 1, {383, y, 431, y + 30}, "+", [this, &amount, step, limit] {
            end_amount_edit();
            amount = static_cast<std::int32_t>(std::min<std::int64_t>(limit, std::int64_t(amount) + step));
        });
        button(first_id + 4, {159, y, 375, y + 30},
               editing_amount_ == &amount ? amount_text_ + "|" : std::to_string(amount),
               [this, &amount, limit] {
                   editing_amount_ = &amount;
                   amount_limit_ = limit;
                   amount_text_.clear();
                   require(SDL_StartTextInput(window_.get()));
               });
        if (show_all)
            button(first_id + 5, {439, y, 533, y + 30}, "全部", [this, &amount, limit] {
                end_amount_edit();
                amount = limit;
            });
    }
    fs::path save_path() const {
        return options_.save_directory / "Save1.DAT";
    }
    fs::path load_path() const {
        if (!options_.load.empty())
            return options_.load;
        // Continue from whichever slot is newer: Android backgrounding writes only the autosave.
        const auto automatic = options_.save_directory / "Autosave.DAT";
        if (fs::exists(save_path()) && fs::exists(automatic))
            return fs::last_write_time(automatic) > fs::last_write_time(save_path()) ? automatic : save_path();
        return fs::exists(automatic) ? automatic : save_path();
    }
    void rotate(unsigned increment) {
        rotation_ = (rotation_ + increment) % 8;
        if (session_)
            session_->set_camera_rotation(static_cast<std::uint8_t>(rotation_));
    }
    void new_game() {
        options_.load.clear();
        NewGameOptions configuration;
        const auto time = std::time(nullptr);
        const auto local = *std::localtime(&time);
        configuration.date = {static_cast<std::uint8_t>(local.tm_mday),
                              static_cast<std::uint8_t>(local.tm_mon + 1),
                              static_cast<std::uint16_t>(local.tm_year + 1900)};
        configuration.map_id = static_cast<std::uint16_t>(selected_map_);
        configuration.stage_id = static_cast<std::uint16_t>(selected_stage_);
        configuration.random_seed = options_.seed;
        configuration.players[0].character = static_cast<std::uint8_t>(selected_character_);
        unsigned candidate = 0;
        for (std::size_t slot = 1; slot < configuration.players.size(); ++slot) {
            while (candidate == selected_character_)
                ++candidate;
            configuration.players[slot].character = static_cast<std::uint8_t>(candidate++);
        }
        install_session(
            World::from_map_resource(artwork_.archive('m').decode((selected_stage_ * 4 + selected_map_) * 2 + 1),
                                     configuration),
            options_.seed);
        ++new_games_;
    }
    void install_session(World world, std::uint32_t seed) {
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
        observe();
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
    void observe() {
        for (const auto &event : session_->drain_events()) {
            ++event_counts_[event.kind];
            if (event.kind == GameEventKind::dice_rolled && event.player < dice_by_player_.size())
                ++dice_by_player_[event.player];
            if (auto item = messages::from_event(event, session_->world()))
                messages_.push(std::move(*item)); // Voices play when their bubble appears.
            if (sound_effects_) {
                switch (event.kind) {
                case GameEventKind::dice_rolled:
                    audio_->play_sound(10);
                    break;
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
            if (event.kind == GameEventKind::unsupported_event)
                unsupported_.push_back(event.message);
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
    }
    void draw() {
        buttons_.clear();
        messages_.retire();
        canvas_ = graphics::make_canvas(640, 480, ui::color(7, 30, 55));
        frame_.ticks = SDL_GetTicks();
        if (screen_ == Screen::menu || screen_ == Screen::new_game ||
            (screen_ == Screen::settings && !session_)) {
            picture(artwork_.frame('d', 1, 0), 0, 0);
        } else if (session_)
            draw_board();
        if (screen_ == Screen::menu) {
            hit(1, {125, 320, 255, 440}, [this] { screen_ = session_ ? Screen::game : Screen::new_game; });
            hit(2, {270, 320, 400, 440}, [this] { load(load_path()); }, fs::exists(load_path()));
            hit(3, {420, 320, 530, 440}, [this] { screen_ = Screen::settings; });
            hit(4, {15, 325, 105, 440}, [this] { screen_ = Screen::new_game; });
        } else if (screen_ == Screen::new_game) {
            dialog({92, 65, 548, 417}, "開始新遊戲");
            font_.draw(canvas_, "選擇角色", 116, 118, 16);
            for (unsigned index = 0; index < 12; ++index) {
                const int x = 114 + int(index % 4) * 104, y = 150 + int(index / 4) * 41;
                button(10 + int(index), {x, y, x + 96, y + 33}, names[index],
                       [this, index] { selected_character_ = index; });
                if (index == selected_character_)
                    ui::border(canvas_, {x + 2, y + 2, x + 94, y + 31}, ui::blue);
            }
            font_.draw(canvas_, "地圖", 116, 285);
            for (unsigned index = 0; index < 4; ++index) {
                const int x = 175 + int(index) * 83;
                button(30 + int(index), {x, 280, x + 75, 312}, map_names[index],
                       [this, index] { selected_map_ = index; });
                if (index == selected_map_)
                    ui::border(canvas_, {x + 2, 282, x + 73, 310}, ui::blue);
            }
            button(42, {114, 320, 522, 345}, selected_stage_ ? "地圖版本：第二組" : "地圖版本：第一組",
                   [this] { selected_stage_ ^= 1; });
            button(40, {160, 350, 290, 391}, "開始", [this] { new_game(); });
            button(41, {345, 350, 475, 391}, "返回", [this] { screen_ = Screen::menu; });
        } else if (screen_ == Screen::settings) {
            buttons_.clear();
            dialog({150, 85, 490, 415}, "設定");
            font_.draw(canvas_, "遊戲速度", 175, 142);
            button(50, {290, 132, 450, 169},
                   speed_ == 1   ? "正常"
                   : speed_ == 2 ? "快速"
                                 : "最快",
                   [this] { speed_ = speed_ == 4 ? 1 : speed_ * 2; });
            button(53, {175, 190, 465, 227}, audio_->music_enabled() ? "背景音樂：開" : "背景音樂：關",
                   [this] { audio_->set_music(!audio_->music_enabled()); });
            button(54, {175, 241, 465, 278}, sound_effects_ ? "音效與語音：開" : "音效與語音：關",
                   [this] { sound_effects_ = !sound_effects_; });
            button(51, {195, 292, 445, 329}, "切換全螢幕", [this] {
                fullscreen_ = !fullscreen_;
                require(SDL_SetWindowFullscreen(window_.get(), fullscreen_));
            });
            button(52, {235, 355, 405, 395}, "返回",
                   [this] { screen_ = session_ ? Screen::game : Screen::menu; });
        } else if (screen_ == Screen::wealth) {
            buttons_.clear();
            dialog({65, 75, 575, 425}, "玩家財產");
            int y = 128;
            for (const auto &player : session_->world().players) {
                font_.draw(canvas_, names.at(player.state.character), 90, y, 18);
                font_.draw(canvas_, "現金 " + ui::money(player.state.cash), 235, y, 16);
                font_.draw(canvas_, "存款 " + ui::money(player.state.bank_balance), 235, y + 23, 16);
                y += 58;
            }
            button(60, {230, 375, 410, 407}, "返回", [this] { screen_ = Screen::game; });
        } else if (screen_ == Screen::cards || screen_ == Screen::tools) {
            draw_inventory();
        } else if (screen_ == Screen::stocks) {
            draw_stocks();
        } else if (screen_ == Screen::trading) {
            draw_trading();
        }
        std::string overlay_key;
        if (screen_ == Screen::game && session_ && messages_.active()) {
            const auto kind = messages_.current()->kind;
            if (messages_.draw(frame_)) {
                static constexpr std::array<const char *, 5> kinds{"bubble", "box", "news", "fortune", "god"};
                overlay_key = std::string("overlay-") + kinds.at(static_cast<std::size_t>(kind));
            }
        }
        if (!toast_.empty() && SDL_GetTicks() < toast_until_) {
            ui::fill(canvas_, {210, 439, 430, 474}, ui::paper);
            ui::border(canvas_, {210, 439, 430, 474}, ui::blue);
            font_.draw(canvas_, toast_, 320 - font_.measure(toast_) / 2, 447);
        }
        const auto pixels = graphics::to_argb8888(canvas_);
        require(SDL_UpdateTexture(texture_.get(), nullptr, pixels.data(), 640 * 4));
        require(SDL_SetRenderDrawColor(renderer_.get(), 0, 0, 0, 255));
        require(SDL_RenderClear(renderer_.get()));
        require(SDL_RenderTexture(renderer_.get(), texture_.get(), nullptr, nullptr));
        require(SDL_RenderPresent(renderer_.get()));
        ++rendered_frames_;
        if (options_.auto_rounds && !options_.output.empty()) {
            auto key = "screen-" + std::to_string(static_cast<int>(screen_));
            if (screen_ == Screen::game && session_ && session_->pending_decision())
                key = "decision-" + std::to_string(static_cast<int>(session_->pending_decision()->kind)) +
                      (selected_target_ ? "-selected" : "");
            if (!overlay_key.empty()) // First few of each overlay kind, for visual review.
                key = overlay_key + "-" + std::to_string(std::min(++overlay_captures_[overlay_key], 4U));
            if (captured_frames_.insert(key).second)
                graphics::write_ppm(canvas_, options_.output / (key + ".ppm"));
        }
    }

    void draw_board() {
        const auto &world = session_->world();
        sync_decision();
        const auto &active = world.active_player().state;
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
        for (int index = 0; index < 11; ++index) {
            const int x = index * 40;
            const std::array labels{"選單", "存檔", "讀檔", "左轉", "右轉", "財產",
                                    "設定", "卡片", "道具", "股票", "買賣"};
            const std::array<std::function<void()>, 11> actions{[this] { screen_ = Screen::menu; },
                                                                [this] { save(); },
                                                                [this] { load(load_path()); },
                                                                [this] { rotate(7); },
                                                                [this] { rotate(1); },
                                                                [this] { screen_ = Screen::wealth; },
                                                                [this] { screen_ = Screen::settings; },
                                                                [this] { screen_ = Screen::cards; },
                                                                [this] { screen_ = Screen::tools; },
                                                                [this] { screen_ = Screen::stocks; },
                                                                [this] {
                                                                    if (session_->open_trading()) {
                                                                        screen_ = Screen::trading;
                                                                        trading_mode_ = 0;
                                                                        trading_page_ = 0;
                                                                    }
                                                                }};
            const bool enabled = index == 1   ? session_->can_save()
                                 : index == 2 ? fs::exists(load_path())
                                 : index >= 7 ? session_->phase() == GamePhase::human_ready
                                              : true;
            button(70 + index, {x, 0, x + 40, 39}, labels[index], actions[index], enabled);
        }
        picture(artwork_.frame('p', 0, 0), 440, 0);
        const auto &portrait = artwork_.frame('m', 27 + active.character, 0);
        graphics::blit(canvas_, portrait, 480, 38);
        font_.draw(canvas_, names.at(active.character), 520, 27, 16);
        font_.draw(canvas_, "現 金", 452, 79, 12);
        font_.draw(canvas_, ui::money(active.cash), 492, 102, 20);
        font_.draw(canvas_, "存 款", 452, 142, 12);
        font_.draw(canvas_, ui::money(active.bank_balance), 492, 165, 20);
        font_.draw(canvas_, "總資產", 452, 205, 12);
        font_.draw(canvas_, ui::money(stocks::player_wealth(world, active.slot)), 480, 228, 20);
        font_.draw(canvas_, "物價指數 " + std::to_string(world.header.price_index), 450, 260, 12);
        ui::fill(canvas_, {440, 282, 640, 480}, ui::paper);
        const auto &date = world.header.date;
        font_.draw(canvas_,
                   std::to_string(date.year) + "/" + std::to_string(date.month) + "/" +
                       std::to_string(date.day),
                   453, 293, 18, ui::blue);
        font_.draw(canvas_, "點數 " + std::to_string(active.points), 453, 321);
        const auto traffic = active.traffic_method & 3U;
        const unsigned maximum_dice = traffic == 1 ? 2U : traffic == 2 ? 3U : 1U;
        button(
            101, {541, 315, 632, 342}, std::to_string(active.dice_count) + " 顆骰子",
            [this, maximum_dice] {
                const auto next = session_->world().active_player().state.dice_count % maximum_dice + 1;
                const bool success = session_->set_dice_count(next);
                if (success)
                    ++dice_selections_;
                notify_action(success);
            },
            maximum_dice > 1 && session_->phase() == GamePhase::human_ready);
        int y = 350;
        const std::size_t first = recent_.size() > 3 ? recent_.size() - 3 : 0;
        for (std::size_t index = first; index < recent_.size(); ++index) {
            font_.draw(canvas_, recent_[index], 452, y, 12, ui::ink, 174);
            y += 37;
        }
        if (session_->phase() == GamePhase::human_ready || session_->phase() == GamePhase::rolling) {
            const auto count = session_->phase() == GamePhase::rolling
                                   ? std::max<std::size_t>(1, session_->dice().size())
                                   : 1;
            for (std::size_t i = 0; i < count; ++i) {
                const int face = session_->dice().empty() ? 0 : session_->dice().at(i) - 1;
                picture(artwork_.frame('p', 7, static_cast<std::size_t>(std::clamp(face, 0, 5))),
                        220 - int(count) * 36 + int(i) * 72, 128);
            }
            hit(
                100, {184, 128, 256, 195}, [this] { session_->roll_dice(); },
                session_->phase() == GamePhase::human_ready);
        }
        if (screen_ == Screen::game && session_->pending_decision() && !messages_.active())
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
    void draw_inventory() {
        buttons_.clear();
        const bool cards = screen_ == Screen::cards;
        const auto &player = session_->world().active_player().state;
        dialog({65, 55, 575, 445}, cards ? "卡片" : "道具");
        std::vector<std::pair<unsigned, unsigned>> inventory;
        if (cards) {
            std::map<unsigned, unsigned> amounts;
            for (const auto id : player.cards)
                if (id)
                    ++amounts[id];
            for (const auto &item : amounts)
                inventory.push_back(item);
        } else {
            for (unsigned i = 0; i < services::tool_names.size(); ++i)
                if (player.tool_amounts[i])
                    inventory.emplace_back(i + 1, player.tool_amounts[i]);
        }
        if (inventory.empty())
            font_.draw(canvas_, cards ? "尚未持有卡片" : "尚未持有道具", 215, 210, 20);
        for (std::size_t index = 0; index < inventory.size(); ++index) {
            const auto [id, amount] = inventory[index];
            const auto label =
                std::string(cards ? services::card_names.at(id - 1) : services::tool_names.at(id - 1)) +
                " ×" + std::to_string(amount) +
                (cards && !active_card(static_cast<std::uint8_t>(id)) ? "（自動）" : "");
            const int x = 86 + int(index % 2) * 235, y = 106 + int(index / 2) * 34;
            button(
                300 + int(id), {x, y, x + 223, y + 30}, label,
                [this, cards, id] {
                    screen_ = Screen::game;
                    notify_action(cards ? session_->use_card({static_cast<std::uint8_t>(id), {}, {}})
                                        : session_->use_tool(id));
                },
                !cards || active_card(static_cast<std::uint8_t>(id)));
        }
        if (!cards && player.traffic_method)
            button(340, {89, 391, 285, 425}, "下車", [this] { notify_action(session_->dismount()); });
        button(60, {340, 391, 545, 425}, "返回", [this] { screen_ = Screen::game; });
    }
    void draw_stocks() {
        buttons_.clear();
        const auto &world = session_->world();
        const auto &player = world.active_player();
        const auto name_offset = std::size_t(world.header.stage_id * 4 + world.header.map_id) * 12;
        dialog({35, 45, 605, 474}, "股票");
        font_.draw(canvas_, "存款 " + ui::money(player.state.bank_balance), 320, 64, 16);
        font_.draw(canvas_, "公司                    現價          漲跌          持有", 60, 99, 14);
        for (std::size_t index = 0; index < world.stocks.size(); ++index) {
            const int y = 124 + int(index) * 20;
            const auto &stock = world.stocks[index];
            const float price = std::bit_cast<float>(stock.float_bits[2]);
            const float movement = std::bit_cast<float>(stock.float_bits[4]);
            std::ostringstream quote, change;
            quote << std::fixed << std::setprecision(2) << price;
            change << std::showpos << std::fixed << std::setprecision(1) << movement << '%';
            if (selected_stock_ == index)
                ui::fill(canvas_, {52, y - 2, 587, y + 18}, ui::color(226, 206, 154));
            font_.draw(canvas_, native_stock_names.at(name_offset + index), 62, y, 14);
            font_.draw(canvas_, quote.str(), 268, y, 14);
            font_.draw(canvas_, stock.suspension_days ? "停牌" : change.str(), 385, y, 14,
                       movement > 0   ? ui::color(175, 35, 20)
                       : movement < 0 ? ui::color(20, 113, 50)
                                      : ui::ink);
            font_.draw(canvas_, std::to_string(player.stocks[index].amount), 505, y, 14);
            hit(400 + int(index), {52, y - 2, 587, y + 18}, [this, index] {
                end_amount_edit();
                selected_stock_ = index;
            });
        }
        font_.draw(canvas_, "股數", 58, 378, 14);
        const auto &selected = world.stocks.at(selected_stock_);
        const float quote = std::bit_cast<float>(selected.float_bits[2]);
        const auto buy_max = std::min<std::int32_t>(
            selected.flags_10, quote > 0 ? static_cast<std::int32_t>(std::min<double>(
                                               INT32_MAX, player.state.bank_balance / double(quote)))
                                         : 0);
        const auto sell_max = player.stocks.at(selected_stock_).amount;
        amount_control(stock_amount_, std::max(buy_max, sell_max), 100, 370);
        button(430, {55, 420, 153, 456}, "買入", [this] { trade(true, stock_amount_); });
        button(431, {163, 420, 261, 456}, "賣出", [this] { trade(false, stock_amount_); });
        button(432, {271, 420, 369, 456}, "全買", [this, buy_max] { trade(true, buy_max); });
        button(433, {379, 420, 477, 456}, "全賣", [this, sell_max] { trade(false, sell_max); });
        button(60, {487, 420, 585, 456}, "返回", [this] { close_stocks(); });
    }
    void close_stocks() {
        end_amount_edit();
        screen_ = Screen::game;
        notify_action(session_->close_stock_market(), "目前無法完成股市結算");
    }
    void trade(bool buy, std::int32_t quantity) {
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
    }
    std::string offer_name(OfferKind kind, std::uint16_t item) const {
        const auto &world = session_->world();
        if (kind == OfferKind::stock)
            return std::string(
                native_stock_names.at((world.header.stage_id * 4 + world.header.map_id) * 12 + item));
        if (kind == OfferKind::tool)
            return std::string(services::tool_names.at(item - 1));
        if (kind == OfferKind::card)
            return std::string(services::card_names.at(item - 1));
        if (kind == OfferKind::property && item > 4000)
            return ui::big5(world.facilities.at(item - 4000).name);
        return "土地 " + std::to_string(item - 2000);
    }
    void choose_listing(trading::Item item) {
        end_amount_edit();
        trading_item_ = item;
        trading_quantity_ = 1;
        const auto limits = trading::listing_limits(
            session_->world(), session_->world().header.current_player, item.kind, item.id, 1);
        trading_price_ = limits ? limits->reference_price : 1;
        trading_mode_ = 2;
    }
    void close_trading() {
        end_amount_edit();
        screen_ = Screen::game;
        notify_action(session_->close_trading(), "目前無法完成買賣結算");
    }
    void draw_trading() {
        buttons_.clear();
        const auto &world = session_->world();
        const auto actor = world.header.current_player;
        dialog({35, 45, 605, 466}, trading_mode_ == 0 ? "玩家買賣" : "刊登出售");
        font_.draw(canvas_, "現金 " + ui::money(world.active_player().state.cash), 350, 63, 16);
        if (trading_mode_ == 2) {
            font_.draw(canvas_, offer_name(trading_item_.kind, trading_item_.id), 80, 128, 20);
            if (trading_item_.kind == OfferKind::stock) {
                font_.draw(canvas_, "股數", 80, 165, 18);
                amount_control(trading_quantity_, trading_item_.maximum_quantity, 100, 195, true, 650);
            }
            const auto limits = trading::listing_limits(world, actor, trading_item_.kind, trading_item_.id,
                                                        static_cast<std::uint16_t>(trading_quantity_));
            font_.draw(canvas_, "出售總價", 80, 280, 18);
            amount_control(trading_price_, limits ? limits->maximum_price : 0, 1000, 318);
            button(
                454, {95, 390, 295, 430}, "刊登",
                [this] {
                    end_amount_edit();
                    const bool success =
                        session_->post_offer({trading_item_.kind, trading_item_.id, trading_price_,
                                              static_cast<std::uint16_t>(trading_quantity_)});
                    if (success) {
                        ++offers_posted_;
                        trading_mode_ = 0;
                        trading_page_ = 0;
                    }
                    notify_action(success, "售價無效或七個刊登欄位已滿");
                },
                limits.has_value() && trading_price_ > 0);
            button(455, {325, 390, 535, 430}, "取消", [this] {
                end_amount_edit();
                trading_mode_ = 0;
            });
            return;
        }
        constexpr std::size_t page_size = 7;
        const auto quotes = trading::quotes(world);
        const auto items = trading::listable_items(world, actor);
        const auto count = trading_mode_ == 0 ? quotes.size() : items.size();
        trading_page_ = count ? std::min(trading_page_, (count - 1) / page_size) : 0;
        const auto first = trading_page_ * page_size;
        if (!count)
            font_.draw(canvas_, trading_mode_ == 0 ? "目前沒有出售物品" : "目前沒有可出售物品", 205, 205, 18);
        for (std::size_t i = first; i < std::min(count, first + page_size); ++i) {
            const int y = 108 + int(i - first) * 36;
            if (trading_mode_ == 1) {
                const auto item = items[i];
                button(460 + int(i), {65, y, 575, y + 31}, offer_name(item.kind, item.id),
                       [this, item] { choose_listing(item); });
            } else {
                const auto quote = quotes[i];
                const auto &offer = quote.offer;
                auto label = offer_name(offer.kind, offer.item);
                if (offer.kind == OfferKind::stock)
                    label += " ×" + std::to_string(offer.quantity);
                font_.draw(canvas_, names.at(world.players.at(quote.seller).state.character), 57, y + 8, 12,
                           ui::ink, 78);
                font_.draw(canvas_, label, 143, y + 8, 14, ui::ink, 178);
                font_.draw(canvas_, ui::money(offer.price), 325, y + 8, 14);
                const bool own = quote.seller == actor;
                button(
                    600 + int(i), {478, y, 581, y + 31}, own ? "撤下" : "買入",
                    [this, quote, own] {
                        const bool success = own ? session_->cancel_offer(quote.index)
                                                 : session_->buy_offer(quote.seller, quote.index);
                        if (success) {
                            if (own)
                                ++offers_cancelled_;
                            else
                                ++offers_bought_;
                        }
                        notify_action(success, "現金或庫存空間不足，請重新選擇");
                    },
                    own || (offer.price > 0 && world.active_player().state.cash >= offer.price));
            }
        }
        button(482, {65, 367, 200, 397}, "上一頁", [this] { --trading_page_; }, trading_page_ > 0);
        button(483, {440, 367, 575, 397}, "下一頁", [this] { ++trading_page_; }, first + page_size < count);
        button(480, {95, 410, 295, 449}, trading_mode_ ? "返回買賣" : "刊登出售", [this] {
            trading_mode_ = trading_mode_ ? 0 : 1;
            trading_page_ = 0;
        });
        button(481, {325, 410, 535, 449}, "返回遊戲", [this] { close_trading(); });
    }
    void sync_decision() {
        if (!session_->pending_decision()) {
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
        for (std::size_t index = 0; index < decision.options.size(); ++index) {
            const auto &option = decision.options[index];
            if (option.location) {
                const auto projected =
                    board::project({option.location->x, option.location->y}, current_camera_);
                if (projected && projected->x >= 18 && projected->x < 422 && projected->y >= 60 &&
                    projected->y < 455) {
                    const auto point = *projected;
                    const auto color = selected_target_ == index ? ui::blue : ui::color(144, 38, 17);
                    ui::fill(canvas_, {point.x - 13, point.y - 12, point.x + 14, point.y + 11}, ui::paper);
                    ui::border(canvas_, {point.x - 14, point.y - 13, point.x + 15, point.y + 12}, color);
                    const auto label = std::to_string(index + 1);
                    font_.draw(canvas_, label, point.x - font_.measure(label, 12) / 2, point.y - 8, 12,
                               color);
                    hit(5000 + int(index), {point.x - 14, point.y - 13, point.x + 15, point.y + 12},
                        [this, index] { select_target(index); });
                }
            }
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
        if (map_decision()) {
            draw_map_decision();
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
        dialog({72, top, 570, bottom}, decision.kind == DecisionKind::bank ? "銀行" : "請選擇");
        font_.draw(canvas_, decision.prompt, 97, top + 50, 18, ui::ink, 448);
        if (decision.kind == DecisionKind::bank || decision.kind == DecisionKind::primary_stock) {
            const bool primary = decision.kind == DecisionKind::primary_stock;
            const auto &player = session_->world().players.at(decision.player).state;
            const auto maximum = primary ? decision.max_amount
                                         : std::max({player.cash, player.bank_balance, player.loan, 1000000});
            amount_control(bank_amount_, maximum, primary ? 100 : 1000, multiple ? 160 : 220, primary);
            if (!primary && multiple) {
                button(207, {93, 346, 313, 378}, "填入全部現金", [this, amount = player.cash] {
                    end_amount_edit();
                    bank_amount_ = amount;
                });
                button(208, {325, 346, 545, 378}, "填入全部存款", [this, amount = player.bank_balance] {
                    end_amount_edit();
                    bank_amount_ = amount;
                });
            }
        } else if (decision.kind == DecisionKind::shop || decision.kind == DecisionKind::rescue)
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
    void respond(std::size_t index) {
        const auto &decision = session_->pending_decision();
        if (!decision)
            return;
        end_amount_edit();
        const auto amount =
            decision->kind == DecisionKind::bank || decision->kind == DecisionKind::primary_stock
                ? bank_amount_
                : 0;
        if (!session_->respond(index, amount)) {
            toast_ = "金額、點數或持有數量不足";
            toast_until_ = SDL_GetTicks() + 2200;
        }
    }
    void push_button(int id) {
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
                require(SDL_PushEvent(&event));
                return;
            }
        throw std::runtime_error("Native UI replay cannot find enabled button " + std::to_string(id));
    }
    void drive_test() {
        if (test_stage_ == 0) {
            push_button(options_.start_new || options_.load.empty() ? 1 : 2);
            test_stage_ = 1;
            return;
        }
        if (screen_ == Screen::new_game) {
            push_button(40);
            return;
        }
        if (!session_) {
            if (load_failed_)
                throw std::runtime_error("Native UI replay could not load its initial save");
            return;
        }
        if (screen_ == Screen::game && messages_.active())
            return; // An overlay is showing for exactly this frame (instant mode); controls are covered.
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
        if (screen_ == Screen::cards || screen_ == Screen::tools) {
            push_button(300 + int(test_inventory_id_));
            return;
        }
        if (screen_ == Screen::stocks) {
            stock_amount_ = 1;
            if (test_stock_stage_ == 1) {
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
            if (trading_mode_ == 1) {
                push_button(460);
                return;
            }
            if (trading_mode_ == 2) {
                push_button(454);
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
                    trading_page_ = i / 7;
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
                selected_stock_ = 7;
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
                if (bank_done_for_visit_) {
                    for (std::size_t index = 0; index < decision->options.size(); ++index)
                        if (!decision->options[index].value)
                            choice = index;
                } else {
                    bank_amount_ = 1000;
                    const auto &player = session_->world().active_player().state;
                    if (player.cash < bank_amount_)
                        for (std::size_t index = 0; index < decision->options.size(); ++index)
                            if (decision->options[index].value == 0)
                                choice = index;
                    bank_done_for_visit_ = true;
                }
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
    messages::Overlay messages_{{[this](std::size_t entry) { return sound_effects_ ? audio_->play_speech(entry) : 0U; },
                                 [this] { audio_->stop_voice(); }}};
    std::map<std::string, unsigned> overlay_captures_;
    graphics::Image canvas_;
    std::vector<Button> buttons_;
    native::Frame frame_{canvas_, artwork_, font_, buttons_};
    Screen screen_ = Screen::menu;
    unsigned selected_character_ = 10, selected_map_ = 0, selected_stage_ = 0, rotation_ = 0, speed_ = 1;
    bool running_ = true, fullscreen_ = false, bank_done_for_visit_ = false, shop_done_for_visit_ = false,
         sound_effects_ = true;
    std::int32_t bank_amount_ = 1000;
    std::int32_t stock_amount_ = 100, amount_limit_ = 0;
    std::int32_t *editing_amount_ = nullptr;
    std::string amount_text_;
    std::size_t selected_stock_ = 0;
    unsigned trading_mode_ = 0;
    std::size_t trading_page_ = 0;
    trading::Item trading_item_{};
    std::int32_t trading_quantity_ = 1, trading_price_ = 1;
    std::uint64_t offers_posted_ = 0, offers_cancelled_ = 0, offers_bought_ = 0;
    std::set<std::pair<std::size_t, std::size_t>> test_trade_attempts_;
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
    std::map<GameEventKind, std::uint64_t> event_counts_;
    std::array<std::uint64_t, 4> dice_by_player_{};
    std::uint64_t input_events_ = 0, rendered_frames_ = 0, new_games_ = 0, loads_ = 0, saves_ = 0;
    std::uint64_t cards_used_ = 0, tools_used_ = 0, stocks_bought_ = 0, stocks_sold_ = 0;
    std::uint64_t dice_selections_ = 0;
    std::set<unsigned> card_ids_used_, tool_ids_used_;
    unsigned test_inventory_id_ = 0, test_stock_stage_ = 0;
    bool tested_cards_ = false, tested_tools_ = false, tested_dice_ = false;
    std::string test_progress_;
    unsigned test_unchanged_frames_ = 0;
    bool load_failed_ = false, test_batched_cancel_ = false;
    bool test_cancel_pending_ = false;
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
                     "  --headless --auto-rounds N --output DIR [--start-new] [--seed N]\n";
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
