/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include <emscripten.h>

#include "emp/base/vector.hpp"
#include "emp/web/Animate.hpp"
#include "emp/web/Button.hpp"
#include "emp/web/Canvas.hpp"
#include "emp/web/Div.hpp"
#include "emp/web/Document.hpp"
#include "emp/web/Image.hpp"
#include "emp/web/Input.hpp"
#include "emp/web/Selector.hpp"
#include "emp/web/Text.hpp"
#include "emp/web/emfunctions.hpp"

#include "core/Avida.hpp"
#include "core/PopulationViewOptions.hpp"

#include "Modules/DriverBuffered.hpp"
#include "Modules/EnvironmentLogic.hpp"
#include "Modules/EventManager.hpp"
#include "Modules/MutationsDivideSub.hpp"
#include "Modules/OrgTypeAvidian.hpp"
#include "Modules/PopGrid.hpp"
#include "Modules/ReactionsManager.hpp"
#include "Modules/TrackGeneration.hpp"
#include "Modules/TrackGenotypes.hpp"
#include "Modules/TrackMetabolism.hpp"

namespace UI = emp::web;

template <typename AVIDA_T>
class WebInterfaceBridge : public ModuleBase<AVIDA_T> {
private:
  std::function<void()> before_exit_callback;

public:
  WebInterfaceBridge(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(
        avida,
        "WebInterfaceBridge",
        "Interface",
        "Connect Avida lifecycle signals to the web interface."
      ) { }

  void Serialize(emp::SerialPod & /* pod */) { }

  void SetBeforeExitCallback(std::function<void()> callback) {
    before_exit_callback = std::move(callback);
  }

  void BeforeExit() {
    if (before_exit_callback) before_exit_callback();
  }
};

using avida_t = Avida<
  OrgTypeAvidian,
  PopGrid,
  DriverBuffered,
  MutationsDivideSub,
  TrackGeneration,
  TrackGenotypes,
  EventManager,
  EnvironmentLogic,
  ReactionsManager,
  TrackMetabolism,
  WebInterfaceBridge
>;
using reaction_config_t = typename ReactionsManager<avida_t>::Config;
using event_config_t = typename EventManager<avida_t>::Config;

constexpr uint32_t PackRGBA(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint32_t>(red)
    | (static_cast<uint32_t>(green) << 8)
    | (static_cast<uint32_t>(blue) << 16)
    | (static_cast<uint32_t>(255) << 24);
}

EM_JS(void, RenderPopulationPixels,
      (const uint32_t * pixels, int width, int height), {
  const render = () => {
    const canvas = document.getElementById('population_canvas');
    if (!canvas) {
      requestAnimationFrame(render);
      return;
    }

    const byteLength = width * height * 4;
    const wasmPixels = new Uint8ClampedArray(HEAPU8.buffer, pixels, byteLength);
    const image = new ImageData(new Uint8ClampedArray(wasmPixels), width, height);
    const context = canvas.getContext('2d');
    context.imageSmoothingEnabled = false;
    context.putImageData(image, 0, 0);
  };
  render();
});

EM_JS(bool, ConfirmPopulationRestart, (), {
  return window.confirm('Restart this population? All evolution so far will be lost.');
});

EM_JS(void, SetConfigurationControlValue,
      (const char * control_id, const char * value), {
  const control = document.getElementById(UTF8ToString(control_id));
  if (control) control.value = UTF8ToString(value);
});

EM_JS(void, ResizePopulationDisplay, (int width, int height), {
  const canvas = document.getElementById('population_canvas');
  if (canvas) {
    canvas.width = width;
    canvas.height = height;
  }

  const surface = document.getElementById('population_grid_surface');
  if (surface) {
    surface.style.setProperty('--grid-cell-width', `${100 / width}%`);
    surface.style.setProperty('--grid-cell-height', `${100 / height}%`);
  }
});

class AvidaWebApp {
private:
  enum class RunMode { PAUSED, PLAY, FAST_FORWARD };
  enum class ColorScale { UNIFORM, CATEGORICAL, CONTINUOUS };
  enum class ConfigurationTab { SETTINGS, ENVIRONMENT, EVENTS };

  static constexpr double PLAY_INTERVAL_MS = 100.0;
  static constexpr double FAST_FORWARD_FRAME_BUDGET_MS = 12.0;
  static constexpr size_t FAST_FORWARD_REDRAW_UPDATES = 10;

  static constexpr uint32_t EMPTY_COLOR = PackRGBA(12, 30, 46);
  static constexpr uint32_t DEFAULT_ORG_COLOR = PackRGBA(110, 205, 224);
  static constexpr uint32_t LOW_FITNESS_COLOR = PackRGBA(73, 210, 218);
  static constexpr uint32_t MID_FITNESS_COLOR = PackRGBA(246, 211, 70);
  static constexpr uint32_t HIGH_FITNESS_COLOR = PackRGBA(186, 84, 198);
  static constexpr std::array<uint32_t, 12> CATEGORY_COLORS{
    PackRGBA(186, 84, 198),
    PackRGBA(73, 210, 218),
    PackRGBA(246, 211, 70),
    PackRGBA(239, 115, 91),
    PackRGBA(126, 105, 208),
    PackRGBA(74, 176, 124),
    PackRGBA(240, 153, 55),
    PackRGBA(86, 139, 219),
    PackRGBA(214, 91, 143),
    PackRGBA(152, 199, 74),
    PackRGBA(120, 215, 179),
    PackRGBA(192, 139, 225)
  };

  std::unique_ptr<avida_t> avida;
  PopulationViewOptions<avida_t> population_view_options;
  std::map<emp::String, emp::String> default_setting_values;
  emp::vector<reaction_config_t> reaction_configs;
  emp::vector<reaction_config_t> default_reaction_configs;
  emp::vector<event_config_t> event_configs;
  emp::vector<event_config_t> default_event_configs;

  UI::Document document{"emp_base"};
  UI::Animate animation;
  UI::Button restart_button;
  UI::Button step_button;
  UI::Button play_button;
  UI::Button pause_button;
  UI::Button fast_forward_button;
  UI::Button pop_stats_mode;
  UI::Button configure_mode;
  UI::Button advanced_toggle;
  UI::Button reset_configuration_button;
  UI::Selector color_selector{"population_color_mode"};
  UI::Div run_inspector;
  UI::Div configuration_inspector;
  emp::vector<UI::Input> configuration_inputs;
  emp::vector<UI::Selector> configuration_selectors;
  emp::vector<UI::Text> statistic_texts;

  RunMode run_mode = RunMode::PAUSED;
  ColorScale active_color_scale = ColorScale::UNIFORM;
  size_t active_color_mode = 0;
  size_t last_grid_redraw_update = 0;
  double play_elapsed_ms = 0.0;
  emp::vector<uint32_t> population_pixels;
  emp::vector<double> continuous_values;
  emp::vector<emp::String> final_statistic_values;
  bool has_final_snapshot = false;
  bool configuration_visible = false;
  bool advanced_settings_visible = false;
  bool run_started = false;
  bool interface_rebuild_requested = false;
  bool structured_config_initialized = false;
  ConfigurationTab active_configuration_tab = ConfigurationTab::SETTINGS;

  [[nodiscard]] avida_t & Avida() { return *avida; }
  [[nodiscard]] const avida_t & Avida() const { return *avida; }
  [[nodiscard]] auto & Grid() { return Avida().GetPlugIn<PopGrid>(); }
  [[nodiscard]] auto & Reactions() { return Avida().GetPlugIn<ReactionsManager>(); }
  [[nodiscard]] auto & Events() { return Avida().GetPlugIn<EventManager>(); }

  void RequestInterfaceRebuild() {
    if (interface_rebuild_requested) return;
    interface_rebuild_requested = true;
    emp::DelayCall([this](){
      if (!interface_rebuild_requested) return;
      interface_rebuild_requested = false;
      RebuildInterface();
    }, 0);
  }

  void CollectPopulationViewOptions() {
    population_view_options = PopulationViewOptions<avida_t>{};
    population_view_options.AddStatistic(
      "update", "Update", "Current population update.",
      [this](){ return emp::MakeString(Avida().GetUpdate()); }
    );
    population_view_options.AddStatistic(
      "organisms", "Organisms", "Number of living organisms.",
      [this](){ return emp::MakeString(Avida().GetNumOrgs()); }
    );
    Avida().TriggerSignal([this](auto & module) {
      if constexpr (requires { module.SetupPopulationView(population_view_options); }) {
        module.SetupPopulationView(population_view_options);
      }
    });
  }

  void ApplyReactionConfiguration() {
    emp_assert(!run_started);
    Reactions().SetConfigs(reaction_configs);
    CollectPopulationViewOptions();
  }

  void ApplyEventConfiguration() {
    Events().SetConfigs(event_configs);
  }

  void ApplyStructuredConfiguration() {
    ApplyReactionConfiguration();
    ApplyEventConfiguration();
  }

  void RefreshReadouts() {
    for (auto & text : statistic_texts) text.Redraw();
  }

  [[nodiscard]] emp::String GetStatisticValue(size_t statistic_id) const {
    if (has_final_snapshot) {
      emp_assert(statistic_id < final_statistic_values.size());
      return final_statistic_values[statistic_id];
    }
    return population_view_options.GetStatistics()[statistic_id].get_value();
  }

  void CaptureFinalView() {
    const auto & statistics = population_view_options.GetStatistics();
    final_statistic_values.clear();
    final_statistic_values.reserve(statistics.size());
    for (const auto & statistic : statistics) {
      final_statistic_values.push_back(statistic.get_value());
    }

    // Preserve the final colored population before Avida clears the biota and trait registry.
    DrawPopulation();
    has_final_snapshot = true;
  }

  void UpdateControls() {
    const bool complete = Avida().IsComplete();
    restart_button.SetDisabled(!run_started);
    step_button.SetDisabled(complete || run_mode != RunMode::PAUSED);
    play_button.SetDisabled(complete);
    pause_button.SetDisabled(complete || run_mode == RunMode::PAUSED);
    fast_forward_button.SetDisabled(complete);

    play_button.SetAttr(
      "class",
      run_mode == RunMode::PLAY
        ? "transport-button icon-button is-active"
        : "transport-button icon-button"
    );
    pause_button.SetAttr(
      "class",
      run_mode == RunMode::PAUSED
        ? "transport-button icon-button is-active"
        : "transport-button icon-button"
    );
    fast_forward_button.SetAttr(
      "class",
      run_mode == RunMode::FAST_FORWARD
        ? "transport-button icon-button is-active"
        : "transport-button icon-button"
    );
    play_button.SetAttr("aria-pressed", run_mode == RunMode::PLAY ? "true" : "false");
    pause_button.SetAttr("aria-pressed", run_mode == RunMode::PAUSED ? "true" : "false");
    fast_forward_button.SetAttr(
      "aria-pressed",
      run_mode == RunMode::FAST_FORWARD ? "true" : "false"
    );
  }

  void SetRunMode(RunMode new_mode) {
    if (new_mode != RunMode::PAUSED && !run_started) StartRun();
    if (new_mode != RunMode::PAUSED && Avida().ConsumePauseRequest()) {
      new_mode = RunMode::PAUSED;
    }
    if (Avida().IsComplete()) new_mode = RunMode::PAUSED;
    run_mode = new_mode;
    play_elapsed_ms = 0.0;

    // Update the button state before Start(), which immediately invokes the
    // animation callback and may spend a long time in fast-forward mode.
    UpdateControls();
    RefreshReadouts();

    if (run_mode == RunMode::PAUSED) {
      if (animation.GetActive()) animation.Stop();
    } else if (!animation.GetActive()) {
      animation.Start();
    }
  }

  [[nodiscard]] static uint32_t BlendColors(uint32_t first, uint32_t second, double amount) {
    const auto blend_channel = [amount](uint32_t a, uint32_t b, size_t shift) {
      const double first_channel = static_cast<double>((a >> shift) & 0xffU);
      const double second_channel = static_cast<double>((b >> shift) & 0xffU);
      return static_cast<uint8_t>(first_channel + (second_channel - first_channel) * amount);
    };
    return PackRGBA(
      blend_channel(first, second, 0),
      blend_channel(first, second, 8),
      blend_channel(first, second, 16)
    );
  }

  [[nodiscard]] static uint32_t GetContinuousColor(double value,
                                                    double minimum,
                                                    double maximum) {
    if (!std::isfinite(value)) return DEFAULT_ORG_COLOR;
    const double normalized = maximum > minimum
      ? std::clamp((value - minimum) / (maximum - minimum), 0.0, 1.0)
      : 0.5;
    if (normalized < 0.5) {
      return BlendColors(LOW_FITNESS_COLOR, MID_FITNESS_COLOR, normalized * 2.0);
    }
    return BlendColors(MID_FITNESS_COLOR, HIGH_FITNESS_COLOR, normalized * 2.0 - 1.0);
  }

  [[nodiscard]] uint32_t GetCategoricalColor(const avida_t::organism_t & organism) const {
    const auto & color_modes = population_view_options.GetCategoricalColorModes();
    if (active_color_mode >= color_modes.size()) return DEFAULT_ORG_COLOR;

    const size_t category = color_modes[active_color_mode].get_category(organism);
    if (category == PopulationViewOptions<avida_t>::NO_CATEGORY) return DEFAULT_ORG_COLOR;
    return CATEGORY_COLORS[category % CATEGORY_COLORS.size()];
  }

  void DrawPopulation() {
    const size_t width = Grid().GetWidth();
    const size_t height = Grid().GetHeight();
    ResizePopulationDisplay(static_cast<int>(width), static_cast<int>(height));

    if (has_final_snapshot) {
      RenderPopulationPixels(
        population_pixels.data(),
        static_cast<int>(width),
        static_cast<int>(height)
      );
      return;
    }

    const std::span<const size_t> cells = Grid().GetCells();
    if (!run_started || cells.size() != width * height) {
      population_pixels.assign(width * height, EMPTY_COLOR);
      continuous_values.clear();
      RenderPopulationPixels(
        population_pixels.data(), static_cast<int>(width), static_cast<int>(height)
      );
      last_grid_redraw_update = 0;
      return;
    }

    population_pixels.resize(cells.size(), EMPTY_COLOR);
    continuous_values.resize(cells.size(), std::numeric_limits<double>::quiet_NaN());

    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    const auto & continuous_modes = population_view_options.GetContinuousColorModes();
    if (active_color_scale == ColorScale::CONTINUOUS
        && active_color_mode < continuous_modes.size()) {
      for (size_t cell_id = 0; cell_id < cells.size(); ++cell_id) {
        const size_t org_id = cells[cell_id];
        if (org_id == PopGrid<avida_t>::EMPTY_CELL || !Avida().IsOccupied(org_id)) continue;

        const double value = continuous_modes[active_color_mode].get_value(Avida().GetOrg(org_id));
        continuous_values[cell_id] = value;
        if (std::isfinite(value)) {
          minimum = std::min(minimum, value);
          maximum = std::max(maximum, value);
        }
      }
    }

    for (size_t cell_id = 0; cell_id < cells.size(); ++cell_id) {
      const size_t org_id = cells[cell_id];
      if (org_id != PopGrid<avida_t>::EMPTY_CELL && Avida().IsOccupied(org_id)) {
        if (active_color_scale == ColorScale::CATEGORICAL) {
          population_pixels[cell_id] = GetCategoricalColor(Avida().GetOrg(org_id));
        } else if (active_color_scale == ColorScale::CONTINUOUS) {
          population_pixels[cell_id] = GetContinuousColor(
            continuous_values[cell_id], minimum, maximum
          );
        } else {
          population_pixels[cell_id] = DEFAULT_ORG_COLOR;
        }
      }
    }

    RenderPopulationPixels(
      population_pixels.data(),
      static_cast<int>(width),
      static_cast<int>(height)
    );
    last_grid_redraw_update = Avida().GetUpdate();
  }

  void FinishUpdate(bool can_continue,
                    bool redraw_population,
                    bool pause_requested = false) {
    if (redraw_population) DrawPopulation();
    RefreshReadouts();
    pause_requested = Avida().ConsumePauseRequest() || pause_requested;
    if (!can_continue || pause_requested) SetRunMode(RunMode::PAUSED);
  }

  void StepPopulation() {
    emp_assert(run_mode == RunMode::PAUSED);
    if (!run_started) StartRun();
    (void) Avida().ConsumePauseRequest();  // An explicit step advances past a start-time pause.
    FinishUpdate(Avida().AdvanceUpdate(), true);
    UpdateControls();
  }

  [[nodiscard]] std::map<emp::String, emp::String> SnapshotSettingValues() const {
    std::map<emp::String, emp::String> values;
    const auto & settings = Avida().GetSettings();
    for (const emp::String & name : settings.GetSettingNames()) {
      values.emplace(name, settings.Get<emp::String>(name));
    }
    return values;
  }

  void CreateConfiguredAvida(const std::map<emp::String, emp::String> & values = {}) {
    if (avida) {
      Avida().GetPlugIn<WebInterfaceBridge>().SetBeforeExitCallback({});
      avida.reset();
    }

    avida = std::make_unique<avida_t>();
    auto & settings = Avida().GetSettings();
    settings.Set("base.config_dir", std::string{"/config"});
    settings.Set("base.data_dir", std::string{"/data"});
    settings.Load("/config/Avida-web.cfg");
    if (structured_config_initialized) {
      Reactions().SetConfigs(reaction_configs);
      Events().SetConfigs(event_configs);
    } else {
      reaction_configs = Reactions().GetConfigs();
      default_reaction_configs = reaction_configs;
      event_configs = Events().GetConfigs();
      default_event_configs = event_configs;
      structured_config_initialized = true;
    }
    for (const auto & [name, value] : values) {
      if (settings.HasSetting(name)) settings.Set(name, value);
    }

    run_mode = RunMode::PAUSED;
    run_started = false;
    has_final_snapshot = false;
    last_grid_redraw_update = 0;
    play_elapsed_ms = 0.0;
    population_pixels.clear();
    continuous_values.clear();
    final_statistic_values.clear();
    CollectPopulationViewOptions();
  }

  void StartRun() {
    emp_assert(!run_started);
    ApplyStructuredConfiguration();
    Avida().GetPlugIn<WebInterfaceBridge>().SetBeforeExitCallback(
      [this](){ CaptureFinalView(); }
    );
    Avida().InitializePaused();
    run_started = true;
    CollectPopulationViewOptions();
    RequestInterfaceRebuild();
  }

  void RestartPopulation() {
    if (!ConfirmPopulationRestart()) return;
    const auto current_values = SnapshotSettingValues();
    SetRunMode(RunMode::PAUSED);
    CreateConfiguredAvida(current_values);
    RequestInterfaceRebuild();
  }

  [[nodiscard]] static emp::String HumanizeSettingName(emp::String name) {
    for (char & character : name) if (character == '_') character = ' ';
    if (name.size()) name[0] = static_cast<char>(std::toupper(name[0]));
    return name;
  }

  [[nodiscard]] static emp::String FormatFixedPoint(double value) {
    if (!std::isfinite(value)) return emp::MakeString(value);
    std::string formatted = std::format("{}", value);
    const size_t exponent_pos = formatted.find_first_of("eE");
    if (exponent_pos == std::string::npos) return formatted;

    const int exponent = std::stoi(formatted.substr(exponent_pos + 1));
    std::string mantissa = formatted.substr(0, exponent_pos);
    std::string sign;
    if (mantissa.size() && (mantissa[0] == '-' || mantissa[0] == '+')) {
      if (mantissa[0] == '-') sign = "-";
      mantissa.erase(0, 1);
    }

    const size_t point_pos = mantissa.find('.');
    const int initial_point = point_pos == std::string::npos
      ? static_cast<int>(mantissa.size())
      : static_cast<int>(point_pos);
    if (point_pos != std::string::npos) mantissa.erase(point_pos, 1);
    const int final_point = initial_point + exponent;

    if (final_point <= 0) {
      return emp::String{sign + "0." + std::string(-final_point, '0') + mantissa};
    }
    if (final_point >= static_cast<int>(mantissa.size())) {
      return emp::String{
        sign + mantissa + std::string(final_point - static_cast<int>(mantissa.size()), '0')
      };
    }
    mantissa.insert(static_cast<size_t>(final_point), ".");
    return emp::String{sign + mantissa};
  }

  [[nodiscard]] emp::String GetDisplaySettingValue(const emp::String & setting_name) const {
    const auto & settings = Avida().GetSettings();
    if (settings.GetTypeName(setting_name) == "double") {
      return FormatFixedPoint(settings.Get<double>(setting_name));
    }
    return settings.Get<emp::String>(setting_name);
  }

  [[nodiscard]] bool ShouldShowSetting(const emp::String & setting_name) const {
    const auto & metadata = Avida().GetSettings().Metadata(setting_name);
    if (metadata.HasTag("local only")) return false;
    if (metadata.HasTag("advanced") && !advanced_settings_visible) return false;
    return true;
  }

  void ToggleAdvancedSettings() {
    advanced_settings_visible = !advanced_settings_visible;
    RequestInterfaceRebuild();
  }

  void SetConfigurationTab(ConfigurationTab tab) {
    if (active_configuration_tab == tab) return;
    active_configuration_tab = tab;
    RequestInterfaceRebuild();
  }

  void ResetConfiguration() {
    if (active_configuration_tab == ConfigurationTab::SETTINGS) {
      auto & settings = Avida().GetSettings();
      for (const auto & [name, value] : default_setting_values) {
        if (!settings.HasSetting(name)) continue;
        if (run_started && settings.Metadata(name).HasTag("startup only")) continue;
        settings.Set(name, value);
      }
    } else if (!run_started && active_configuration_tab == ConfigurationTab::ENVIRONMENT) {
      reaction_configs = default_reaction_configs;
      ApplyReactionConfiguration();
    } else if (active_configuration_tab == ConfigurationTab::EVENTS) {
      event_configs = default_event_configs;
      ApplyEventConfiguration();
    }
    RequestInterfaceRebuild();
  }

  void UpdateReaction(size_t reaction_id, auto update_fun) {
    if (run_started || reaction_id >= reaction_configs.size()) return;
    update_fun(reaction_configs[reaction_id]);
    ApplyReactionConfiguration();
  }

  void AddReaction() {
    if (run_started) return;
    const emp::String task = Avida().GetNumTasks() ? Avida().GetTaskName(0) : "";
    const auto trait_names = Avida().GetTraitNames<double>();
    const emp::String trait = std::find(trait_names.begin(), trait_names.end(), "metabolic_mult")
        != trait_names.end()
      ? emp::String{"metabolic_mult"}
      : (trait_names.size() ? trait_names[0] : emp::String{});
    reaction_configs.push_back({
      .task_name = task,
      .trait_name = trait,
      .operation = "mult",
      .value = 2.0,
      .max_triggers = 1
    });
    ApplyReactionConfiguration();
    RequestInterfaceRebuild();
  }

  void RemoveReaction(size_t reaction_id) {
    if (run_started || reaction_id >= reaction_configs.size()) return;
    reaction_configs.erase(reaction_configs.begin() + reaction_id);
    ApplyReactionConfiguration();
    RequestInterfaceRebuild();
  }

  void UpdateEvent(size_t event_id, auto update_fun, bool rebuild_interface = false) {
    if (event_id >= event_configs.size()) return;
    update_fun(event_configs[event_id]);
    ApplyEventConfiguration();
    if (rebuild_interface) RequestInterfaceRebuild();
  }

  void AddEvent() {
    const size_t pause_update = Avida().GetUpdate() + (run_started ? 1000 : 10000);
    event_configs.push_back({
      .timing = EventManager<avida_t>::Timing::UPDATE,
      .start = pause_update,
      .interval = 1,
      .stop = 0,
      .command = "pause"
    });
    ApplyEventConfiguration();
    RequestInterfaceRebuild();
  }

  void RemoveEvent(size_t event_id) {
    if (event_id >= event_configs.size()) return;
    event_configs.erase(event_configs.begin() + event_id);
    ApplyEventConfiguration();
    RequestInterfaceRebuild();
  }

  void SetConfigurationValue(const emp::String & setting_name,
                             const std::string & value,
                             const emp::String & linked_control_id = "") {
    if (value.empty()) return;
    Avida().GetSettings().Set(setting_name, emp::String{value});

    if (linked_control_id.size()) {
      const emp::String current_value = GetDisplaySettingValue(setting_name);
      SetConfigurationControlValue(linked_control_id.c_str(), current_value.c_str());
    }

    if (!run_started && (setting_name == "grid.width" || setting_name == "grid.height")) {
      DrawPopulation();
    }
  }

  void SetConfigurationVisible(bool visible) {
    configuration_visible = visible;
    run_inspector.SetCSS("display", visible ? "none" : "block");
    configuration_inspector.SetCSS("display", visible ? "block" : "none");
    pop_stats_mode.SetAttr(
      "class",
      visible ? "mode-button side-mode-button" : "mode-button side-mode-button is-active"
    );
    configure_mode.SetAttr(
      "class",
      visible ? "mode-button side-mode-button is-active" : "mode-button side-mode-button"
    );
    pop_stats_mode.SetAttr("aria-pressed", visible ? "false" : "true");
    configure_mode.SetAttr("aria-pressed", visible ? "true" : "false");
  }

  void AddConfigurationSetting(UI::Div & scope_panel,
                               const emp::String & setting_name,
                               const emp::String & local_name,
                               size_t setting_id) {
    const auto & settings = Avida().GetSettings();
    const auto & metadata = settings.Metadata(setting_name);
    const emp::String current_value = GetDisplaySettingValue(setting_name);
    const emp::String raw_value = settings.Get<emp::String>(setting_name);
    const std::string type_name = settings.GetTypeName(setting_name);
    const bool is_integer = type_name == "int64_t" || type_name == "uint64_t";
    const bool is_numeric = is_integer || type_name == "double";
    const bool is_locked = run_started && metadata.HasTag("startup only");
    const emp::String control_id = emp::MakeString("configuration_control_", setting_id);

    UI::Div setting_panel{emp::MakeString("configuration_setting_", setting_id)};
    setting_panel.AddAttr(
      "class", is_locked ? "configuration-setting is-locked" : "configuration-setting"
    );
    if (is_locked) setting_panel.SetTitle("This setting is locked after a run starts.");
    setting_panel << emp::MakeString(
      "<label class='configuration-label' for='", control_id, "'>",
      emp::MakeWebSafe(HumanizeSettingName(local_name)), "</label>"
    );
    if (settings.GetDesc(setting_name).size()) {
      setting_panel << emp::MakeString(
        "<p class='configuration-description'>",
        emp::MakeWebSafe(settings.GetDesc(setting_name)), "</p>"
      );
    }

    UI::Div controls{emp::MakeString("configuration_controls_", setting_id)};
    controls.AddAttr("class", "configuration-controls");

    const auto & options = metadata.GetOptions();
    if (options.size() && !metadata.AllowsOtherOptions()) {
      UI::Selector selector{control_id};
      size_t selected_id = 0;
      for (size_t option_id = 0; option_id < options.size(); ++option_id) {
        const emp::String option = options[option_id];
        if (option == raw_value) selected_id = option_id;
        selector.SetOption(emp::MakeWebSafe(option), [this, setting_name, option]() {
          SetConfigurationValue(setting_name, option);
        });
      }
      selector.SelectID(selected_id);
      selector.Disabled(is_locked);
      selector.AddAttr("class", "configuration-select");
      selector.SetAttr("aria-label", HumanizeSettingName(local_name));
      configuration_selectors.push_back(selector);
      controls << configuration_selectors.back();
    } else if (is_numeric && metadata.HasMinimum() && metadata.HasMaximum()) {
      const emp::String number_id = emp::MakeString(control_id, "_number");
      UI::Input slider{
        [this, setting_name, number_id](std::string value) {
          SetConfigurationValue(setting_name, value, number_id);
        },
        "range", "", control_id
      };
      slider.Min(metadata.GetMinimum());
      slider.Max(metadata.GetMaximum());
      slider.Step(is_integer ? "1" : "any");
      slider.Value(current_value);
      slider.Disabled(is_locked);
      slider.AddAttr("class", "configuration-slider");
      slider.SetAttr("aria-label", HumanizeSettingName(local_name));

      UI::Input number{
        [this, setting_name, control_id](std::string value) {
          SetConfigurationValue(setting_name, value, control_id);
        },
        "number", "", number_id
      };
      number.Min(metadata.GetMinimum());
      number.Max(metadata.GetMaximum());
      number.Step(is_integer ? "1" : "any");
      number.Value(current_value);
      number.Disabled(is_locked);
      number.AddAttr("class", "configuration-number");
      number.SetAttr("onwheel", "this.blur()");
      number.SetAttr(
        "aria-label", emp::MakeString("Type value for ", HumanizeSettingName(local_name))
      );

      configuration_inputs.push_back(slider);
      controls << configuration_inputs.back();
      configuration_inputs.push_back(number);
      controls << configuration_inputs.back();
    } else {
      UI::Input input{
        [this, setting_name](std::string value) {
          SetConfigurationValue(setting_name, value);
        },
        is_numeric ? "number" : "text", "", control_id
      };
      if (is_numeric) input.Step(is_integer ? "1" : "any");
      if (metadata.HasMinimum()) input.Min(metadata.GetMinimum());
      if (metadata.HasMaximum()) input.Max(metadata.GetMaximum());
      input.Value(current_value);
      input.Disabled(is_locked);
      input.AddAttr("class", "configuration-input");
      if (is_numeric) input.SetAttr("onwheel", "this.blur()");
      input.SetAttr("aria-label", HumanizeSettingName(local_name));
      configuration_inputs.push_back(input);
      controls << configuration_inputs.back();
    }

    setting_panel << controls;
    scope_panel << setting_panel;
  }

  void BuildSettingsConfiguration(UI::Div & content) {
    content << "<p class='configuration-intro'>Settings for the current population.</p>";
    const auto setting_names = Avida().GetSettings().GetSettingNames();
    std::map<emp::String, emp::vector<emp::String>> settings_by_scope;
    for (const emp::String & setting_name : setting_names) {
      if (!ShouldShowSetting(setting_name)) continue;
      const size_t separator = setting_name.find('.');
      const emp::String scope = separator == emp::String::npos
        ? emp::String{"General"}
        : setting_name.substr(0, separator);
      settings_by_scope[scope].push_back(setting_name);
    }

    size_t setting_id = 0;
    for (const auto & [scope_name, scoped_settings] : settings_by_scope) {
      UI::Div scope_panel{emp::MakeString("configuration_scope_", setting_id)};
      scope_panel.AddAttr("class", "configuration-scope");
      const emp::String heading_id = emp::MakeString("configuration_scope_heading_", setting_id);
      scope_panel.SetAttr("role", "group");
      scope_panel.SetAttr("aria-labelledby", heading_id);
      scope_panel << emp::MakeString(
        "<h3 id='", heading_id, "'>", emp::MakeWebSafe(HumanizeSettingName(scope_name)), "</h3>"
      );

      for (const emp::String & setting_name : scoped_settings) {
        const size_t separator = setting_name.find('.');
        const emp::String local_name = separator == emp::String::npos
          ? setting_name
          : setting_name.substr(separator + 1);
        AddConfigurationSetting(scope_panel, setting_name, local_name, setting_id++);
      }
      content << scope_panel;
    }
  }

  void BuildEnvironmentConfiguration(UI::Div & content) {
    content << emp::MakeString(
      "<p class='configuration-intro'>Reactions connect completed tasks to phenotype changes.",
      run_started ? " Reactions are locked after a run starts." : "", "</p>"
    );

    UI::Button add_button{[this](){ AddReaction(); }, "+ Add reaction", "add_reaction_button"};
    add_button.AddAttr("class", "configuration-add-button");
    add_button.SetDisabled(run_started);
    content << add_button;

    UI::Div reaction_list{"reaction_configuration_list"};
    reaction_list.AddAttr("class", "structured-configuration-list");
    for (size_t reaction_id = 0; reaction_id < reaction_configs.size(); ++reaction_id) {
      const reaction_config_t & reaction = reaction_configs[reaction_id];
      UI::Div card{emp::MakeString("reaction_configuration_", reaction_id)};
      card.AddAttr(
        "class",
        run_started ? "structured-configuration-card is-locked" : "structured-configuration-card"
      );

      UI::Div card_header{emp::MakeString("reaction_header_", reaction_id)};
      card_header.AddAttr("class", "structured-configuration-header");
      card_header << emp::MakeString("<h3>Reaction ", reaction_id + 1, "</h3>");
      UI::Button remove_button{
        [this, reaction_id](){ RemoveReaction(reaction_id); },
        "Remove",
        emp::MakeString("remove_reaction_", reaction_id)
      };
      remove_button.AddAttr("class", "configuration-remove-button");
      remove_button.SetDisabled(run_started);
      card_header << remove_button;
      card << card_header;

      UI::Div fields{emp::MakeString("reaction_fields_", reaction_id)};
      fields.AddAttr("class", "structured-configuration-fields");

      const emp::String task_id = emp::MakeString("reaction_task_", reaction_id);
      UI::Div task_field{emp::MakeString(task_id, "_field")};
      task_field.AddAttr("class", "structured-configuration-field");
      task_field << emp::MakeString("<label for='", task_id, "'>Task</label>");
      UI::Selector task_selector{task_id};
      size_t selected_task = 0;
      for (size_t id = 0; id < Avida().GetNumTasks(); ++id) {
        const emp::String task_name = Avida().GetTaskName(id);
        if (task_name == reaction.task_name) selected_task = id;
        task_selector.SetOption(task_name, [this, reaction_id, task_name](){
          UpdateReaction(reaction_id, [task_name](auto & config){ config.task_name = task_name; });
        });
      }
      task_selector.SelectID(selected_task);
      task_selector.Disabled(run_started);
      task_selector.AddAttr("class", "configuration-select");
      configuration_selectors.push_back(task_selector);
      task_field << configuration_selectors.back();
      fields << task_field;

      const emp::String triggers_id = emp::MakeString("reaction_max_triggers_", reaction_id);
      UI::Div triggers_field{emp::MakeString(triggers_id, "_field")};
      triggers_field.AddAttr("class", "structured-configuration-field");
      triggers_field << emp::MakeString("<label for='", triggers_id, "'>Max triggers</label>");
      UI::Input triggers_input{
        [this, reaction_id](std::string value){
          if (value.empty()) return;
          UpdateReaction(reaction_id, [value](auto & config){
            config.max_triggers = emp::String{value}.As<size_t>(config.max_triggers);
          });
        },
        "number", "", triggers_id
      };
      triggers_input.Min("0");
      triggers_input.Step("1");
      triggers_input.Value(emp::MakeString(reaction.max_triggers));
      triggers_input.Disabled(run_started);
      triggers_input.AddAttr("class", "configuration-input");
      triggers_input.SetAttr("onwheel", "this.blur()");
      triggers_input.SetTitle("Zero allows unlimited triggers per gestation.");
      configuration_inputs.push_back(triggers_input);
      triggers_field << configuration_inputs.back();
      fields << triggers_field;

      const emp::String operation_id = emp::MakeString("reaction_operation_", reaction_id);
      UI::Div operation_field{emp::MakeString(operation_id, "_field")};
      operation_field.AddAttr("class", "structured-configuration-field");
      operation_field << emp::MakeString("<label for='", operation_id, "'>Operation</label>");
      UI::Selector operation_selector{operation_id};
      operation_selector.SetOption("Multiply", [this, reaction_id](){
        UpdateReaction(reaction_id, [](auto & config){ config.operation = "mult"; });
      });
      operation_selector.SetOption("Add", [this, reaction_id](){
        UpdateReaction(reaction_id, [](auto & config){ config.operation = "add"; });
      });
      operation_selector.SelectID(reaction.operation == "add" ? 1 : 0);
      operation_selector.Disabled(run_started);
      operation_selector.AddAttr("class", "configuration-select");
      configuration_selectors.push_back(operation_selector);
      operation_field << configuration_selectors.back();
      fields << operation_field;

      const emp::String value_id = emp::MakeString("reaction_value_", reaction_id);
      UI::Div value_field{emp::MakeString(value_id, "_field")};
      value_field.AddAttr("class", "structured-configuration-field");
      value_field << emp::MakeString("<label for='", value_id, "'>Value</label>");
      UI::Input value_input{
        [this, reaction_id](std::string value){
          if (value.empty()) return;
          UpdateReaction(reaction_id, [value](auto & config){
            config.value = emp::String{value}.As<double>(config.value);
          });
        },
        "number", "", value_id
      };
      value_input.Step("any");
      value_input.Value(FormatFixedPoint(reaction.value));
      value_input.Disabled(run_started);
      value_input.AddAttr("class", "configuration-input");
      value_input.SetAttr("onwheel", "this.blur()");
      configuration_inputs.push_back(value_input);
      value_field << configuration_inputs.back();
      fields << value_field;

      card << fields;
      reaction_list << card;
    }

    if (reaction_configs.empty()) {
      reaction_list << "<p class='configuration-empty'>No reactions configured.</p>";
    }
    content << reaction_list;
  }

  void AddEventNumberField(UI::Div & fields,
                           size_t event_id,
                           const emp::String & field_name,
                           const emp::String & label,
                           size_t value,
                           auto update_fun) {
    const emp::String control_id = emp::MakeString("event_", field_name, "_", event_id);
    UI::Div field{emp::MakeString(control_id, "_field")};
    field.AddAttr("class", "structured-configuration-field");
    field << emp::MakeString("<label for='", control_id, "'>", label, "</label>");
    UI::Input input{
      [this, event_id, update_fun](std::string new_value){
        if (new_value.empty()) return;
        UpdateEvent(event_id, [new_value, update_fun](auto & config){
          update_fun(config, emp::String{new_value}.As<size_t>());
        });
      },
      "number", "", control_id
    };
    input.Min(field_name == "stop" ? "0" : "1");
    input.Step("1");
    input.Value(emp::MakeString(value));
    input.AddAttr("class", "configuration-input");
    input.SetAttr("onwheel", "this.blur()");
    configuration_inputs.push_back(input);
    field << configuration_inputs.back();
    fields << field;
  }

  void BuildEventsConfiguration(UI::Div & content) {
    content << emp::MakeString(
      "<p class='configuration-intro'>Schedule interface events at run start, run end, or selected updates.",
      run_started ? " New events default to pausing 1,000 updates from now." : "", "</p>"
    );

    UI::Button add_button{[this](){ AddEvent(); }, "+ Add event", "add_event_button"};
    add_button.AddAttr("class", "configuration-add-button");
    content << add_button;

    UI::Div event_list{"event_configuration_list"};
    event_list.AddAttr("class", "structured-configuration-list");
    for (size_t event_id = 0; event_id < event_configs.size(); ++event_id) {
      const event_config_t & event = event_configs[event_id];
      UI::Div card{emp::MakeString("event_configuration_", event_id)};
      card.AddAttr("class", "structured-configuration-card");

      UI::Div card_header{emp::MakeString("event_header_", event_id)};
      card_header.AddAttr("class", "structured-configuration-header");
      card_header << emp::MakeString("<h3>Event ", event_id + 1, "</h3>");
      UI::Button remove_button{
        [this, event_id](){ RemoveEvent(event_id); },
        "Remove",
        emp::MakeString("remove_event_", event_id)
      };
      remove_button.AddAttr("class", "configuration-remove-button");
      card_header << remove_button;
      card << card_header;

      UI::Div fields{emp::MakeString("event_fields_", event_id)};
      fields.AddAttr("class", "structured-configuration-fields");

      const emp::String timing_id = emp::MakeString("event_timing_", event_id);
      UI::Div timing_field{emp::MakeString(timing_id, "_field")};
      timing_field.AddAttr("class", "structured-configuration-field");
      timing_field << emp::MakeString("<label for='", timing_id, "'>When</label>");
      UI::Selector timing_selector{timing_id};
      using Timing = EventManager<avida_t>::Timing;
      const std::array<std::pair<emp::String, Timing>, 4> timing_options{{
        {"At start", Timing::START},
        {"At update", Timing::UPDATE},
        {"At intervals", Timing::INTERVAL},
        {"At end", Timing::END}
      }};
      size_t selected_timing = 0;
      for (size_t id = 0; id < timing_options.size(); ++id) {
        const auto [label, timing] = timing_options[id];
        if (timing == event.timing) selected_timing = id;
        timing_selector.SetOption(label, [this, event_id, timing](){
          UpdateEvent(
            event_id,
            [timing](auto & config){ config.timing = timing; },
            true
          );
        });
      }
      timing_selector.SelectID(selected_timing);
      timing_selector.AddAttr("class", "configuration-select");
      configuration_selectors.push_back(timing_selector);
      timing_field << configuration_selectors.back();
      fields << timing_field;

      const emp::String action_id = emp::MakeString("event_action_", event_id);
      UI::Div action_field{emp::MakeString(action_id, "_field")};
      action_field.AddAttr("class", "structured-configuration-field");
      action_field << emp::MakeString("<label for='", action_id, "'>Action</label>");
      UI::Selector action_selector{action_id};
      action_selector.SetOption("Pause", [this, event_id](){
        UpdateEvent(event_id, [](auto & config){ config.command = "pause"; });
      });
      action_selector.SelectID(0);
      action_selector.AddAttr("class", "configuration-select");
      configuration_selectors.push_back(action_selector);
      action_field << configuration_selectors.back();
      fields << action_field;

      if (event.timing == Timing::UPDATE || event.timing == Timing::INTERVAL) {
        AddEventNumberField(
          fields, event_id, "start", event.timing == Timing::UPDATE ? "Update" : "Start",
          event.start, [](auto & config, size_t value){ config.start = value; }
        );
      }
      if (event.timing == Timing::INTERVAL) {
        AddEventNumberField(
          fields, event_id, "interval", "Every", event.interval,
          [](auto & config, size_t value){ config.interval = std::max<size_t>(1, value); }
        );
        AddEventNumberField(
          fields, event_id, "stop", "Through (0 = forever)", event.stop,
          [](auto & config, size_t value){ config.stop = value; }
        );
      }

      card << fields;
      event_list << card;
    }

    if (event_configs.empty()) {
      event_list << "<p class='configuration-empty'>No events configured.</p>";
    }
    content << event_list;
  }

  void BuildConfigurationPanel() {
    configuration_inspector = UI::Div{"configuration_inspector"};
    configuration_inspector.AddAttr("class", "configuration-inspector");
    configuration_inspector.SetCSS("display", "none");

    configuration_inputs.clear();
    configuration_selectors.clear();
    configuration_inputs.reserve(64);
    configuration_selectors.reserve(64);

    UI::Div configuration_header{"configuration_header"};
    configuration_header.AddAttr("class", "configuration-header");
    configuration_header << "<h2>Configuration</h2>";
    UI::Div configuration_actions{"configuration_actions"};
    configuration_actions.AddAttr("class", "configuration-actions");
    if (active_configuration_tab == ConfigurationTab::SETTINGS) {
      advanced_toggle = UI::Button(
        [this](){ ToggleAdvancedSettings(); },
        advanced_settings_visible ? "Advanced: On" : "Advanced: Off",
        "advanced_settings_toggle"
      );
      advanced_toggle.AddAttr("class", "configuration-action-button");
      advanced_toggle.SetAttr("aria-pressed", advanced_settings_visible ? "true" : "false");
      advanced_toggle.SetTitle("Show or hide advanced settings");
      configuration_actions << advanced_toggle;
    }
    reset_configuration_button = UI::Button(
      [this](){ ResetConfiguration(); }, "Reset", "reset_configuration_button"
    );
    reset_configuration_button.AddAttr("class", "configuration-action-button");
    reset_configuration_button.SetTitle("Reset this configuration tab to its web defaults");
    reset_configuration_button.SetDisabled(
      run_started && active_configuration_tab == ConfigurationTab::ENVIRONMENT
    );
    configuration_actions << reset_configuration_button;
    configuration_header << configuration_actions;
    configuration_inspector << configuration_header;

    UI::Div tabs{"configuration_tabs"};
    tabs.AddAttr("class", "configuration-tabs");
    tabs.SetAttr("role", "tablist");
    const auto add_tab = [this, &tabs](ConfigurationTab tab,
                                       const emp::String & label,
                                       const emp::String & id) {
      UI::Button button{[this, tab](){ SetConfigurationTab(tab); }, label, id};
      button.AddAttr(
        "class",
        active_configuration_tab == tab ? "configuration-tab is-active" : "configuration-tab"
      );
      button.SetAttr("role", "tab");
      button.SetAttr("aria-selected", active_configuration_tab == tab ? "true" : "false");
      tabs << button;
    };
    add_tab(ConfigurationTab::SETTINGS, "Settings", "configuration_tab_settings");
    add_tab(ConfigurationTab::ENVIRONMENT, "Environment", "configuration_tab_environment");
    add_tab(ConfigurationTab::EVENTS, "Events", "configuration_tab_events");
    configuration_inspector << tabs;

    UI::Div content{"configuration_content"};
    content.AddAttr("class", "configuration-content");
    content.SetAttr("role", "tabpanel");
    switch (active_configuration_tab) {
    case ConfigurationTab::SETTINGS: BuildSettingsConfiguration(content); break;
    case ConfigurationTab::ENVIRONMENT: BuildEnvironmentConfiguration(content); break;
    case ConfigurationTab::EVENTS: BuildEventsConfiguration(content); break;
    }
    configuration_inspector << content;
  }

  void OnAnimationFrame(const UI::Animate & frame) {
    if (run_mode == RunMode::PLAY) {
      play_elapsed_ms += frame.GetStepTime();
      if (play_elapsed_ms < PLAY_INTERVAL_MS) return;

      play_elapsed_ms = 0.0;  // Do not catch up after a delayed or backgrounded frame.
      FinishUpdate(Avida().AdvanceUpdate(), true);
      return;
    }

    if (run_mode != RunMode::FAST_FORWARD) return;

    const double frame_start = emp::GetTime();
    bool can_continue = true;
    bool pause_requested = false;
    do {
      can_continue = Avida().AdvanceUpdate();
      pause_requested = Avida().ConsumePauseRequest();
    } while (can_continue
             && !pause_requested
             && emp::GetTime() - frame_start < FAST_FORWARD_FRAME_BUDGET_MS);

    const bool redraw_population = pause_requested
      || !can_continue
      || Avida().GetUpdate() - last_grid_redraw_update >= FAST_FORWARD_REDRAW_UPDATES;
    FinishUpdate(can_continue, redraw_population, pause_requested);
  }

  void SetupColorSelector() {
    color_selector = UI::Selector{"population_color_mode"};
    active_color_scale = ColorScale::UNIFORM;
    active_color_mode = 0;
    color_selector.SetOption("Uniform", [this]() {
      active_color_scale = ColorScale::UNIFORM;
      active_color_mode = 0;
      DrawPopulation();
    });

    const auto & categorical_modes = population_view_options.GetCategoricalColorModes();
    for (size_t mode_id = 0; mode_id < categorical_modes.size(); ++mode_id) {
      color_selector.SetOption(categorical_modes[mode_id].label, [this, mode_id]() {
        active_color_scale = ColorScale::CATEGORICAL;
        active_color_mode = mode_id;
        DrawPopulation();
      });
    }

    const auto & continuous_modes = population_view_options.GetContinuousColorModes();
    for (size_t mode_id = 0; mode_id < continuous_modes.size(); ++mode_id) {
      color_selector.SetOption(continuous_modes[mode_id].label, [this, mode_id]() {
        active_color_scale = ColorScale::CONTINUOUS;
        active_color_mode = mode_id;
        DrawPopulation();
      });
    }

    if (categorical_modes.size()) {
      active_color_scale = ColorScale::CATEGORICAL;
      active_color_mode = 0;
      color_selector.SelectID(1);
    }
    color_selector.SetAttr("aria-label", "Population color mode");
    color_selector.SetTitle("Color organisms by");
  }

  void BuildInterface() {
    UI::Div app{"avida_app"};
    app.AddAttr("class", "avida-app");

    UI::Div header{"app_header"};
    header.AddAttr("class", "app-header");

    UI::Div primary_header{"primary_header"};
    primary_header.AddAttr("class", "primary-header");

    UI::Div brand{"brand"};
    brand.AddAttr("class", "brand");
    UI::Image logo{"assets/icons/LOGO-noBG.png", "avida_logo"};
    logo.Alt("Avida").AddAttr("class", "brand-logo");
    brand << logo;

    UI::Div modes{"mode_buttons"};
    modes.AddAttr("class", "mode-buttons");
    UI::Button population_mode{
      [](){},
      "<img src='assets/icons/PopGrid.png' alt=''><span>POPULATION</span>",
      "population_mode"
    };
    UI::Button organism_mode{
      [](){},
      "<img src='assets/icons/ModeOrganism.png' alt=''><span>ORGANISMS</span>",
      "organism_mode"
    };
    UI::Button analyze_mode{
      [](){},
      "<img src='assets/icons/ModeAnalyze.png' alt=''><span>ANALYZE</span>",
      "analyze_mode"
    };
    population_mode.AddAttr("class", "mode-button is-active");
    organism_mode.AddAttr("class", "mode-button");
    analyze_mode.AddAttr("class", "mode-button");
    population_mode.SetAttr("aria-label", "Population Mode");
    organism_mode.SetAttr("aria-label", "Organism Mode");
    analyze_mode.SetAttr("aria-label", "Analyze Mode");
    population_mode.SetAttr("aria-pressed", "true");
    organism_mode.SetAttr("aria-pressed", "false");
    analyze_mode.SetAttr("aria-pressed", "false");
    population_mode.SetTitle("Population Mode");
    organism_mode.SetTitle("Organism Mode");
    analyze_mode.SetTitle("Analyze Mode");
    modes << population_mode;
    modes << organism_mode;
    modes << analyze_mode;

    UI::Div side_modes{"side_mode_buttons"};
    side_modes.AddAttr("class", "mode-buttons side-mode-buttons");
    pop_stats_mode = UI::Button{
      [this](){ SetConfigurationVisible(false); },
      "<img src='assets/icons/StatsPop.png' alt=''><span>POP STATS</span>",
      "pop_stats_mode"
    };
    UI::Button org_stats_mode{
      [](){},
      "<img src='assets/icons/StatsOrg.png' alt=''><span>ORG STATS</span>",
      "org_stats_mode"
    };
    UI::Button freezer_mode{
      [](){},
      "<img src='assets/icons/Freezer.png' alt=''><span>FREEZER</span>",
      "freezer_mode"
    };
    configure_mode = UI::Button{
      [this](){ SetConfigurationVisible(!configuration_visible); },
      "<img src='assets/icons/Config.png' alt=''><span>CONFIGURE</span>",
      "configure_mode"
    };
    pop_stats_mode.AddAttr("class", "mode-button side-mode-button is-active");
    org_stats_mode.AddAttr("class", "mode-button side-mode-button");
    freezer_mode.AddAttr("class", "mode-button side-mode-button");
    configure_mode.AddAttr("class", "mode-button side-mode-button");
    pop_stats_mode.SetAttr("aria-label", "Population Statistics");
    org_stats_mode.SetAttr("aria-label", "Organism Statistics");
    freezer_mode.SetAttr("aria-label", "Freezer");
    configure_mode.SetAttr("aria-label", "Configure");
    pop_stats_mode.SetAttr("aria-controls", "run_inspector");
    configure_mode.SetAttr("aria-controls", "configuration_inspector");
    pop_stats_mode.SetAttr("aria-pressed", "true");
    org_stats_mode.SetAttr("aria-pressed", "false");
    freezer_mode.SetAttr("aria-pressed", "false");
    configure_mode.SetAttr("aria-pressed", "false");
    pop_stats_mode.SetTitle("Pop Stats");
    org_stats_mode.SetTitle("Org Stats");
    freezer_mode.SetTitle("Freezer");
    configure_mode.SetTitle("Configure");
    side_modes << pop_stats_mode;
    side_modes << org_stats_mode;
    side_modes << freezer_mode;
    side_modes << configure_mode;

    primary_header << brand;
    primary_header << modes;
    header << primary_header;
    header << side_modes;
    app << header;

    UI::Div workspace{"workspace"};
    workspace.AddAttr("class", "workspace");
    UI::Div population_card{"population_card"};
    population_card.AddAttr("class", "population-card");

    UI::Div canvas_frame{"canvas_frame"};
    canvas_frame.AddAttr("class", "canvas-frame");
    UI::Canvas population_canvas{
      static_cast<double>(Grid().GetWidth()),
      static_cast<double>(Grid().GetHeight()),
      "population_canvas"
    };
    population_canvas.AddAttr("class", "population-canvas");
    population_canvas.SetAttr("role", "img");
    population_canvas.SetAttr("aria-label", "Grid population of digital organisms");
    UI::Div population_surface{"population_grid_surface"};
    population_surface.AddAttr("class", "population-grid-surface");
    population_surface.AddAttr(
      "style",
      emp::MakeString(
        "--grid-cell-width: ", 100.0 / Grid().GetWidth(),
        "%; --grid-cell-height: ", 100.0 / Grid().GetHeight(), "%;"
      )
    );
    population_surface << population_canvas;
    canvas_frame << population_surface;

    UI::Div transport{"transport"};
    transport.AddAttr("class", "transport");
    UI::Div transport_buttons{"transport_buttons"};
    transport_buttons.AddAttr("class", "transport-buttons");

    restart_button = UI::Button(
      [this](){ RestartPopulation(); },
      "<span class='restart-icon' aria-hidden='true'></span>",
      "restart_button"
    );
    step_button = UI::Button(
      [this](){ StepPopulation(); },
      "<span class='step-icon' aria-hidden='true'></span>",
      "step_button"
    );
    play_button = UI::Button(
      [this](){ SetRunMode(RunMode::PLAY); }, "&#x25B6;", "play_button"
    );
    pause_button = UI::Button(
      [this](){ SetRunMode(RunMode::PAUSED); }, "&#x275A;&#x275A;", "pause_button"
    );
    fast_forward_button = UI::Button(
      [this](){ SetRunMode(RunMode::FAST_FORWARD); },
      "&#x25B6;&#x25B6;",
      "fast_forward_button"
    );

    restart_button.AddAttr("class", "transport-button icon-button restart-button");
    step_button.AddAttr("class", "transport-button icon-button");
    play_button.AddAttr("class", "transport-button icon-button");
    pause_button.AddAttr("class", "transport-button icon-button is-active");
    fast_forward_button.AddAttr("class", "transport-button icon-button");
    restart_button.SetAttr("aria-label", "Restart population");
    step_button.SetAttr("aria-label", "Advance one population update");
    play_button.SetAttr("aria-label", "Play at up to ten updates per second");
    pause_button.SetAttr("aria-label", "Pause evolution");
    fast_forward_button.SetAttr("aria-label", "Run as fast as possible");
    restart_button.SetTitle("Restart");
    step_button.SetTitle("Step");
    play_button.SetTitle("Play");
    pause_button.SetTitle("Pause");
    fast_forward_button.SetTitle("Fast-forward");
    color_selector.AddAttr("class", "color-selector");
    transport_buttons << restart_button;
    transport_buttons << step_button;
    transport_buttons << play_button;
    transport_buttons << pause_button;
    transport_buttons << fast_forward_button;
    transport_buttons << color_selector;
    transport << transport_buttons;
    population_card << canvas_frame;
    population_card << transport;

    run_inspector = UI::Div{"run_inspector"};
    run_inspector.AddAttr("class", "run-inspector");
    run_inspector << "<h2>Run</h2>";
    UI::Div readouts{"readouts"};
    readouts.AddAttr("class", "readouts");
    const auto & statistics = population_view_options.GetStatistics();
    statistic_texts.reserve(statistics.size());
    for (size_t statistic_id = 0; statistic_id < statistics.size(); ++statistic_id) {
      const auto & statistic = statistics[statistic_id];
      UI::Div readout{emp::MakeString("statistic_", statistic_id)};
      readout.AddAttr("class", "readout");
      readout.SetTitle(statistic.description);
      readout << emp::MakeString("<span>", emp::MakeWebSafe(statistic.label), "</span>");

      UI::Text value_text{emp::MakeString("statistic_value_", statistic_id)};
      value_text << UI::Live([this, statistic_id](){
        return GetStatisticValue(statistic_id);
      });
      readout << value_text;
      statistic_texts.push_back(value_text);
      readouts << readout;
    }
    run_inspector << readouts;

    BuildConfigurationPanel();

    workspace << population_card;
    UI::Div side_panel{"side_panel"};
    side_panel.AddAttr("class", "side-panel");
    side_panel << run_inspector;
    side_panel << configuration_inspector;
    workspace << side_panel;
    app << workspace;
    document << app;
  }

  void RebuildInterface() {
    const bool restore_configuration = configuration_visible;
    document.Freeze();
    document.ClearChildren();
    configuration_inputs.clear();
    configuration_selectors.clear();
    statistic_texts.clear();
    SetupColorSelector();
    BuildInterface();
    SetConfigurationVisible(restore_configuration);
    UpdateControls();
    document.Activate();
    DrawPopulation();
    RefreshReadouts();
  }

public:
  AvidaWebApp()
    : animation([this](const UI::Animate & frame){ OnAnimationFrame(frame); }) { }

  ~AvidaWebApp() {
    if (avida) Avida().GetPlugIn<WebInterfaceBridge>().SetBeforeExitCallback({});
  }

  void Initialize() {
    CreateConfiguredAvida();
    default_setting_values = SnapshotSettingValues();
    std::println(
      "Loaded /config/Avida-web.cfg (substitution probability = {}).",
      Avida().GetSettings().Get<double>("mutations.substitution_prob")
    );
    RebuildInterface();
  }
};

AvidaWebApp avida_web_app;

int emp_main() {
  avida_web_app.Initialize();
}
