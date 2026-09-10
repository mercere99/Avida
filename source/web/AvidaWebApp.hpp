#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <emscripten.h>

#include "EmpiricalWeb.hpp"

#include "WebBuild.hpp"

namespace UI = emp::web;

namespace {
bool capture_checkpoint_warnings = false;
std::string checkpoint_warning_message;

void InstallCheckpointWarningCapture() {
  static const bool installed = []() {
    emp::notify::WarningHandlers().Add([](
      emp::notify::id_arg_t,
      emp::notify::message_arg_t message
    ) {
      if (!capture_checkpoint_warnings) return false;
      if (!checkpoint_warning_message.empty()) checkpoint_warning_message += " ";
      checkpoint_warning_message += message;
      return true;
    });
    return true;
  }();
  (void) installed;
}
}

constexpr uint32_t PackRGBA(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint32_t>(red)
    | (static_cast<uint32_t>(green) << 8)
    | (static_cast<uint32_t>(blue) << 16)
    | (static_cast<uint32_t>(255) << 24);
}

#include "BrowserBridge.hpp"

class AvidaWebApp {
private:
  enum class RunMode { PAUSED, PLAY, FAST_FORWARD };
  enum class OrganismRunMode { PAUSED, PLAY, FAST_FORWARD };
  enum class TrackedOrganismHead { NONE = -1, IP, READ, WRITE, FLOW };
  enum class ApplicationMode { POPULATION, ORGANISM };
  enum class ColorScale { BLANK, CATEGORICAL, CONTINUOUS };
  enum class ConfigurationTab { SETTINGS, ENVIRONMENT, EVENTS };
  enum class SidePanel { POPULATION, ORGANISM, FREEZER, CONFIGURATION };

  struct PlacedOrganism {
    size_t cell_id = 0;
    emp::String name;
    emp::String genome;
    size_t instruction_count = 0;

    void Serialize(emp::SerialPod & pod) {
      pod(cell_id, name, genome, instruction_count);
    }
  };

  struct ContinuousColorRange {
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();

    void Include(double value) {
      if (!std::isfinite(value)) return;
      minimum = std::min(minimum, value);
      maximum = std::max(maximum, value);
    }
  };

  struct CategoricalLegendEntry {
    size_t category = 0;
    size_t count = 0;
    uint32_t color = 0;
    emp::String label;
  };

  struct AnalysisTrait {
    emp::String name;
    emp::String value;
    emp::String description;
  };

  struct AnalysisTaskExecution {
    size_t task_id = 0;
    size_t step = 0;
    bool is_first = false;
  };

  struct FrozenConfiguration {
    std::map<emp::String, emp::String> settings;
    emp::String ancestor_genome;
    emp::vector<reaction_config_t> reactions;
    emp::vector<event_config_t> events;
    emp::vector<PlacedOrganism> placed_organisms;

    void Serialize(emp::SerialPod & pod) {
      pod(settings, ancestor_genome);

      size_t reaction_count = reactions.size();
      pod(reaction_count);
      if (pod.IsLoad()) reactions.resize(reaction_count);
      for (auto & reaction : reactions) {
        pod(
          reaction.task_name,
          reaction.trait_name,
          reaction.operation,
          reaction.value,
          reaction.max_triggers
        );
      }

      size_t event_count = events.size();
      pod(event_count);
      if (pod.IsLoad()) events.resize(event_count);
      for (auto & event : events) {
        pod(
          event.timing,
          event.start,
          event.interval,
          event.stop,
          event.command
        );
      }
      pod(placed_organisms);
    }
  };

  struct FrozenOrganism {
    size_t id = 0;
    emp::String name;
    emp::String genome;
    size_t instruction_count = 0;

    void Serialize(emp::SerialPod & pod) { pod(id, name, genome, instruction_count); }
  };

  struct FrozenConfigurationItem {
    size_t id = 0;
    emp::String name;
    FrozenConfiguration configuration;

    void Serialize(emp::SerialPod & pod) { pod(id, name, configuration); }
  };

  struct FreezerStore {
    size_t next_id = 1;
    emp::vector<FrozenOrganism> organisms;
    emp::vector<FrozenConfigurationItem> configurations;

    void Serialize(emp::SerialPod & pod) {
      pod(next_id, organisms, configurations);
    }
  };

  static constexpr double PLAY_INTERVAL_MS = 100.0;
  static constexpr size_t FAST_FORWARD_REDRAW_UPDATES = 10;
  static constexpr size_t FAST_FORWARD_WORK_BATCH_UPDATES = 32;
  static constexpr size_t ORGANISM_ANALYSIS_STEP_LIMIT = 100000;
  static constexpr double ORGANISM_PLAY_INTERVAL_MS = 500.0;
  static constexpr double ORGANISM_FAST_FORWARD_INTERVAL_MS = 50.0;
  static constexpr size_t CHECKPOINT_CONFIGURATION_SCHEMA = 2;

  static constexpr uint32_t EMPTY_COLOR = PackRGBA(12, 30, 46);
  static constexpr uint32_t DEFAULT_ORG_COLOR = PackRGBA(110, 205, 224);
  static constexpr uint32_t STAGED_ORG_COLOR = PackRGBA(239, 115, 91);
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
  UI::Button org_stats_mode;
  UI::Button freezer_mode;
  UI::Button configure_mode;
  UI::Button organism_step_button;
  UI::Button organism_reset_button;
  UI::Button organism_play_button;
  UI::Button organism_pause_button;
  UI::Button organism_fast_forward_button;
  UI::Button organism_offspring_button;
  UI::Input organism_position_slider;
  UI::Selector organism_freezer_selector{"organism_freezer_selector"};
  UI::Button save_organism_button;
  UI::Button save_configuration_button;
  UI::Button save_run_button;
  UI::Button advanced_toggle;
  UI::Button reset_configuration_button;
  UI::Selector color_selector{"population_color_mode"};
  UI::Div run_inspector;
  UI::Div org_stats_inspector;
  UI::Div freezer_inspector;
  UI::Div configuration_inspector;
  emp::vector<UI::Input> configuration_inputs;
  emp::vector<UI::Selector> configuration_selectors;
  emp::vector<UI::Text> statistic_texts;
  UI::Text population_color_legend;
  UI::Text org_stats_content;
  UI::Text organism_mode_content;

  RunMode run_mode = RunMode::PAUSED;
  OrganismRunMode organism_run_mode = OrganismRunMode::PAUSED;
  ApplicationMode active_application_mode = ApplicationMode::POPULATION;
  ColorScale active_color_scale = ColorScale::CATEGORICAL;
  size_t active_color_mode = 0;
  emp::String active_color_mode_id{"phenotype"};
  size_t last_grid_redraw_update = 0;
  double play_elapsed_ms = 0.0;
  double organism_play_elapsed_ms = 0.0;
  size_t population_pixel_width = 1;
  size_t population_pixel_height = 1;
  emp::vector<uint32_t> population_pixels;
  emp::vector<double> continuous_values;
  emp::vector<std::map<size_t, uint32_t>> categorical_color_maps;
  emp::vector<ContinuousColorRange> continuous_color_ranges;
  emp::vector<emp::String> final_statistic_values;
  emp::String final_color_legend_html;
  bool has_final_snapshot = false;
  bool advanced_settings_visible = false;
  bool run_started = false;
  bool interface_rebuild_requested = false;
  bool structured_config_initialized = false;
  enum class SimulationWorkerState : uint8_t { IDLE, REQUESTED, RUNNING, READY };
  std::atomic<SimulationWorkerState> simulation_worker_state{SimulationWorkerState::IDLE};
  std::atomic<bool> simulation_worker_shutdown{false};
  std::atomic<bool> simulation_can_continue{true};
  std::atomic<bool> simulation_pause_requested{false};
  std::atomic<size_t> simulation_update_budget{1};
  std::mutex simulation_wait_mutex;
  std::condition_variable simulation_wait_condition;
  std::thread simulation_worker;
  std::optional<std::string> pending_checkpoint_import;
  ConfigurationTab active_configuration_tab = ConfigurationTab::SETTINGS;
  SidePanel active_side_panel = SidePanel::POPULATION;
  avida_t::org_ref_t active_organism;
  size_t active_cell_id = avida_web::EMPTY_CELL;
  FreezerStore freezer;
  emp::vector<PlacedOrganism> placed_organisms;
  emp::String freezer_message;
  emp::String configured_ancestor_genome;
  size_t drop_organism_callback_id = 0;
  size_t freeze_grid_callback_id = 0;
  size_t rename_freezer_callback_id = 0;
  size_t checkpoint_file_callback_id = 0;
  size_t population_step_callback_id = 0;
  size_t population_fast_forward_callback_id = 0;
  size_t organism_space_step_callback_id = 0;
  size_t organism_scrub_callback_id = 0;
  size_t organism_head_callback_id = 0;
  size_t grid_context_cell_id = avida_web::EMPTY_CELL;

  std::unique_ptr<avida_t::organism_t> organism_analysis_subject;
  std::optional<AvidaVM> organism_analysis_hardware;
  std::optional<avida_t::genome_t> organism_analysis_offspring;
  emp::String organism_analysis_name;
  emp::String organism_last_instruction;
  emp::String organism_last_description;
  emp::String organism_step_notes;
  size_t organism_last_ip = 0;
  size_t organism_execution_step = 0;
  size_t organism_execution_length = 0;
  bool organism_execution_complete = false;
  TrackedOrganismHead tracked_organism_head = TrackedOrganismHead::IP;
  emp::vector<AnalysisTrait> organism_analysis_traits;
  emp::vector<size_t> organism_task_counts;
  emp::vector<size_t> organism_task_totals;
  emp::vector<size_t> organism_reaction_task_ids;
  emp::vector<AnalysisTaskExecution> organism_task_executions;

  [[nodiscard]] avida_t & Avida() { return *avida; }
  [[nodiscard]] const avida_t & Avida() const { return *avida; }
  [[nodiscard]] auto Organisms() { return organism_adapter_t{Avida()}; }
  [[nodiscard]] auto Population() { return population_adapter_t{Avida()}; }
  [[nodiscard]] size_t PopulationWidth() {
    return has_final_snapshot ? population_pixel_width : Population().GetWidth();
  }
  [[nodiscard]] size_t PopulationHeight() {
    return has_final_snapshot ? population_pixel_height : Population().GetHeight();
  }
  [[nodiscard]] auto & Reactions() { return Avida().GetPlugIn<ReactionsManager>(); }
  [[nodiscard]] auto & Events() { return Avida().GetPlugIn<EventManager>(); }

  [[nodiscard]] bool SimulationWorkerBusy() const;
  void SimulationWorkerLoop();
  void StartSimulationWorker();
  void StopSimulationWorker();
  [[nodiscard]] bool DispatchSimulationUpdate(size_t update_budget);
  [[nodiscard]] std::optional<bool> ConsumeSimulationUpdate();

  [[nodiscard]] static emp::String ReadTextFile(const std::string & filename);

  [[nodiscard]] std::string FreezerStorageKey();

  [[nodiscard]] bool PersistFreezer();

  [[nodiscard]] bool RestoreFreezer();

  [[nodiscard]] FrozenConfiguration SnapshotConfiguration() const;

  void InitializeFreezer();

  [[nodiscard]] static emp::String TrimmedName(emp::String name);

  [[nodiscard]] static emp::String DownloadStem(const emp::String & name);

  [[nodiscard]] avida_checkpoint::Compatibility CheckpointCompatibility();

  [[nodiscard]] static std::string SerializeConfiguration(
    FrozenConfiguration configuration
  );

  [[nodiscard]] std::string BuildCheckpointFile(
    FrozenConfiguration configuration,
    const std::string & state
  );

  [[nodiscard]] static emp::String QuoteConfigString(const emp::String & value);

  template <typename ITEM_T>
  void RenameFrozenItem(emp::vector<ITEM_T> & items, size_t id, emp::String new_name) {
    const auto iterator = std::find_if(items.begin(), items.end(), [id](const auto & item) {
      return item.id == id;
    });
    if (iterator == items.end()) return;
    new_name = TrimmedName(std::move(new_name));
    if (!new_name.size() || new_name == iterator->name) return;

    iterator->name = std::move(new_name);
    (void) PersistFreezer();
    freezer_message = emp::MakeString("Renamed freezer item to ", iterator->name, ".");
    RequestInterfaceRebuild();
  }

  [[nodiscard]] emp::String BuildConfigurationFile(
    const FrozenConfiguration & configuration,
    const emp::String & ancestor_filename
  );

  void DownloadFrozenOrganism(size_t id);

  void DownloadFrozenConfiguration(size_t id);

  [[nodiscard]] static emp::String GenomeFileText(const avida_t::organism_t & organism);

  [[nodiscard]] std::optional<avida_t::genome_t>
  ParseGenomeText(const emp::String & genome_text);

  [[nodiscard]] bool InjectGenomeAtCell(const emp::String & genome_text, size_t cell_id);

  void ApplyPlacedOrganisms();

  [[nodiscard]] auto FindPlacedOrganism(size_t cell_id) {
    return std::find_if(
      placed_organisms.begin(), placed_organisms.end(),
      [cell_id](const auto & item){ return item.cell_id == cell_id; }
    );
  }

  [[nodiscard]] auto FindPlacedOrganism(size_t cell_id) const {
    return std::find_if(
      placed_organisms.cbegin(), placed_organisms.cend(),
      [cell_id](const auto & item){ return item.cell_id == cell_id; }
    );
  }

  [[nodiscard]] bool HasGridCellOrganism(size_t cell_id);

  void OpenGridCellMenu(size_t cell_id, int client_x, int client_y);

  void SaveGridCellOrganism(size_t cell_id);

  void RemoveGridCellOrganism(size_t cell_id);

  void DropFrozenOrganismOnGrid(size_t freezer_id, size_t cell_id);

  void FreezeOrUnstageGridCell(size_t cell_id);

  void InitializeDragCallbacks();

  void SaveActiveOrganism();

  void SaveConfiguration();

  void SaveRun();

  void LoadConfiguration(const FrozenConfiguration & configuration);

  void LoadFrozenOrganism(size_t id);

  void LoadFrozenConfiguration(size_t id);

  template <typename VALUE_T>
  [[nodiscard]] static std::expected<void, std::string> DeserializeCheckpointPart(
    const std::string & bytes,
    VALUE_T & value,
    const std::string & label
  ) {
    std::istringstream input{bytes};
    emp::SerialPod pod{input};
    pod(value);
    if (input.fail()) return std::unexpected("Checkpoint " + label + " could not be decoded.");
    input >> std::ws;
    if (!input.eof()) {
      return std::unexpected("Checkpoint " + label + " contains unread data.");
    }
    return {};
  }

  [[nodiscard]] std::expected<std::unique_ptr<avida_t>, std::string>
  BuildCheckpointCandidate(
    const avida_checkpoint::Contents & contents,
    FrozenConfiguration & configuration
  );

  void ImportCheckpointFile(const std::string & checkpoint);

  template <typename ITEM_T>
  void RemoveFrozenItem(emp::vector<ITEM_T> & items, size_t id) {
    const auto iterator = std::find_if(items.begin(), items.end(), [id](const auto & item) {
      return item.id == id;
    });
    if (iterator == items.end()) return;
    const emp::String removed_name = iterator->name;
    items.erase(iterator);
    (void) PersistFreezer();
    freezer_message = emp::MakeString("Removed ", removed_name, ".");
    RequestInterfaceRebuild();
  }

  [[nodiscard]] const avida_t::organism_t * GetActiveOrganism() const;

  void UpdateActiveCellHighlight();

  void SelectPopulationCell(size_t cell_id);

  void RequestInterfaceRebuild();

  void CollectPopulationViewOptions();

  void ApplyReactionConfiguration();

  void ApplyEventConfiguration();

  void ApplyStructuredConfiguration();

  void RefreshReadouts();

  [[nodiscard]] emp::String GetStatisticValue(size_t statistic_id) const;

  void CaptureFinalView();

  void UpdateControls();

  void SetRunMode(RunMode new_mode);

  void ToggleRunMode(RunMode mode);

  [[nodiscard]] static uint32_t BlendColors(uint32_t first, uint32_t second, double amount);

  [[nodiscard]] static uint32_t GetContinuousColor(double value,
                                                    double minimum,
                                                    double maximum);

  [[nodiscard]] static uint32_t MakeDistinctCategoryColor(size_t category, size_t attempt);

  [[nodiscard]] uint32_t GetDistinctCategoryColor(size_t category);

  [[nodiscard]] uint32_t GetCategoricalColor(const avida_t::organism_t & organism);

  [[nodiscard]] static emp::String ColorToCSS(uint32_t color);

  [[nodiscard]] static emp::String FormatColorScaleValue(double value);

  [[nodiscard]] static emp::String FormatLegendCount(size_t count);

  [[nodiscard]] emp::String GetCategoricalLegendLabel(
    const avida_t::organism_t & organism,
    size_t category
  ) const;

  [[nodiscard]] emp::String BuildPopulationColorLegendHTML();

  void DrawPopulation();

  void FinishUpdate(bool can_continue,
                    bool redraw_population,
                    bool pause_requested = false);

  void StepPopulation();

  [[nodiscard]] std::map<emp::String, emp::String> SnapshotSettingValues() const;

  void CreateConfiguredAvida(
    const std::map<emp::String, emp::String> & values = {},
    const emp::String & ancestor_genome = ""
  );

  void StartRun();

  void RestartPopulation();

  [[nodiscard]] static emp::String HumanizeSettingName(emp::String name);

  [[nodiscard]] static emp::String FormatFixedPoint(double value);

  [[nodiscard]] emp::String GetDisplaySettingValue(const emp::String & setting_name) const;

  [[nodiscard]] bool ShouldShowSetting(const emp::String & setting_name) const;

  void ToggleAdvancedSettings();

  void SetConfigurationTab(ConfigurationTab tab);

  void ResetConfiguration();

  void UpdateReaction(size_t reaction_id, auto update_fun) {
    if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED
        || run_started || reaction_id >= reaction_configs.size()) return;
    update_fun(reaction_configs[reaction_id]);
    ApplyReactionConfiguration();
  }

  void AddReaction();

  void RemoveReaction(size_t reaction_id);

  void UpdateEvent(size_t event_id, auto update_fun, bool rebuild_interface = false) {
    if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED
        || event_id >= event_configs.size()) return;
    update_fun(event_configs[event_id]);
    ApplyEventConfiguration();
    if (rebuild_interface) RequestInterfaceRebuild();
  }

  void AddEvent();

  void RemoveEvent(size_t event_id);

  void SetConfigurationValue(const emp::String & setting_name,
                             const std::string & value,
                             const emp::String & linked_control_id = "");

  [[nodiscard]] bool IsReactionTask(size_t task_id) const;

  void InitializeOrganismAnalysis(
    const avida_t::genome_t & genome,
    const emp::String & name,
    const avida_t::organism_t * trait_source = nullptr
  );

  void SetOrganismExecutionPosition(size_t target_step);

  void FocusTrackedOrganismHead() const;

  void RedrawOrganismModeContent();

  void SetTrackedOrganismHead(size_t head_id);

  void SetOrganismRunMode(OrganismRunMode mode);

  void ToggleOrganismRunMode(OrganismRunMode mode);

  void JumpToOrganismExecutionPosition(const std::string & value);

  [[nodiscard]] bool PrepareSelectedOrganismAnalysis();

  void SelectFrozenOrganismForAnalysis(size_t freezer_id);

  void UpdateOrganismModeControls();

  void StepOrganismInstruction();

  void AdvancePlayingOrganismInstruction();

  void ResetOrganismAnalysis();

  void ViewOrganismOffspring();

  void SetApplicationMode(ApplicationMode mode);

  [[nodiscard]] static emp::String NotesAsHTML(emp::String notes);

  [[nodiscard]] emp::String BuildOrganismModeHTML() const;

  [[nodiscard]] emp::String BuildOrganismStatsHTML();

  void SetSidePanel(SidePanel panel);

  void AddConfigurationSetting(UI::Div & scope_panel,
                               const emp::String & setting_name,
                               const emp::String & local_name,
                               size_t setting_id);

  void BuildSettingsConfiguration(UI::Div & content);

  void BuildEnvironmentConfiguration(UI::Div & content);

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

  void BuildEventsConfiguration(UI::Div & content);

  [[nodiscard]] UI::Div BuildFreezerNameEditor(
    size_t item_type,
    size_t item_id,
    const emp::String & name,
    const emp::String & id_prefix
  );

  void BuildFreezerPanel();

  void BuildConfigurationPanel();

  void OnAnimationFrame(const UI::Animate & frame);

  void SetupColorSelector();

  void BuildOrganismModeWorkspace(UI::Div & app);

  void BuildInterface();

  void RebuildInterface();

public:
  AvidaWebApp()
    : animation([this](const UI::Animate & frame){ OnAnimationFrame(frame); }) { }

  ~AvidaWebApp() {
    StopSimulationWorker();
    if (avida) {
      Avida().GetPlugIn<WebInterfaceBridge>().SetOnStartCallback({});
      Avida().GetPlugIn<WebInterfaceBridge>().SetBeforeExitCallback({});
    }
    if (drop_organism_callback_id) emp::JSDelete(drop_organism_callback_id);
    if (freeze_grid_callback_id) emp::JSDelete(freeze_grid_callback_id);
    if (rename_freezer_callback_id) emp::JSDelete(rename_freezer_callback_id);
    if (checkpoint_file_callback_id) emp::JSDelete(checkpoint_file_callback_id);
    if (population_step_callback_id) emp::JSDelete(population_step_callback_id);
    if (population_fast_forward_callback_id) {
      emp::JSDelete(population_fast_forward_callback_id);
    }
    if (organism_space_step_callback_id) emp::JSDelete(organism_space_step_callback_id);
    if (organism_scrub_callback_id) emp::JSDelete(organism_scrub_callback_id);
    if (organism_head_callback_id) emp::JSDelete(organism_head_callback_id);
  }

  void Initialize();
};

#include "SimulationWorker.hpp"
#include "Freezer.hpp"
#include "Checkpoints.hpp"
#include "OrganismAnalysis.hpp"
#include "PopulationInteraction.hpp"
#include "BrowserCallbacks.hpp"
#include "Layout.hpp"
#include "PopulationView.hpp"
#include "Configuration.hpp"
#include "RunControl.hpp"
#include "ConfigurationPanels.hpp"
#include "FreezerPanel.hpp"
#include "OrganismLayout.hpp"
