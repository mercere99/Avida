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

  [[nodiscard]] avida_t & Avida() { return *avida; }
  [[nodiscard]] const avida_t & Avida() const { return *avida; }
  [[nodiscard]] auto & Grid() { return Avida().GetPlugIn<PopGrid>(); }

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

  void FinishUpdate(bool can_continue, bool redraw_population) {
    if (redraw_population) DrawPopulation();
    RefreshReadouts();
    if (!can_continue) SetRunMode(RunMode::PAUSED);
  }

  void StepPopulation() {
    emp_assert(run_mode == RunMode::PAUSED);
    if (!run_started) StartRun();
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

  void ResetConfiguration() {
    auto & settings = Avida().GetSettings();
    for (const auto & [name, value] : default_setting_values) {
      if (!settings.HasSetting(name)) continue;
      if (run_started && settings.Metadata(name).HasTag("startup only")) continue;
      settings.Set(name, value);
    }
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
      input.SetAttr("aria-label", HumanizeSettingName(local_name));
      configuration_inputs.push_back(input);
      controls << configuration_inputs.back();
    }

    setting_panel << controls;
    scope_panel << setting_panel;
  }

  void BuildConfigurationPanel() {
    configuration_inspector = UI::Div{"configuration_inspector"};
    configuration_inspector.AddAttr("class", "configuration-inspector");
    configuration_inspector.SetCSS("display", "none");

    UI::Div configuration_header{"configuration_header"};
    configuration_header.AddAttr("class", "configuration-header");
    configuration_header << "<h2>Configuration</h2>";
    UI::Div configuration_actions{"configuration_actions"};
    configuration_actions.AddAttr("class", "configuration-actions");
    advanced_toggle = UI::Button(
      [this](){ ToggleAdvancedSettings(); },
      advanced_settings_visible ? "Advanced: On" : "Advanced: Off",
      "advanced_settings_toggle"
    );
    advanced_toggle.AddAttr("class", "configuration-action-button");
    advanced_toggle.SetAttr("aria-pressed", advanced_settings_visible ? "true" : "false");
    advanced_toggle.SetTitle("Show or hide advanced settings");
    reset_configuration_button = UI::Button(
      [this](){ ResetConfiguration(); }, "Reset", "reset_configuration_button"
    );
    reset_configuration_button.AddAttr("class", "configuration-action-button");
    reset_configuration_button.SetTitle("Reset editable settings to the web defaults");
    configuration_actions << advanced_toggle;
    configuration_actions << reset_configuration_button;
    configuration_header << configuration_actions;
    configuration_inspector << configuration_header;
    configuration_inspector <<
      "<p class='configuration-intro'>Settings for the current population.</p>";

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

    configuration_inputs.clear();
    configuration_selectors.clear();
    configuration_inputs.reserve(setting_names.size() * 2);
    configuration_selectors.reserve(setting_names.size());
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
      configuration_inspector << scope_panel;
    }
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
    do {
      can_continue = Avida().AdvanceUpdate();
    } while (can_continue && emp::GetTime() - frame_start < FAST_FORWARD_FRAME_BUDGET_MS);

    const bool redraw_population = !can_continue
      || Avida().GetUpdate() - last_grid_redraw_update >= FAST_FORWARD_REDRAW_UPDATES;
    FinishUpdate(can_continue, redraw_population);
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
    document.ClearChildren();
    configuration_inputs.clear();
    configuration_selectors.clear();
    statistic_texts.clear();
    SetupColorSelector();
    BuildInterface();
    SetConfigurationVisible(restore_configuration);
    UpdateControls();
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
