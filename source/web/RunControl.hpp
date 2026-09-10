#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

void AvidaWebApp::UpdateControls() {
  const bool worker_busy = SimulationWorkerBusy();
  const bool population_running = run_mode != RunMode::PAUSED || worker_busy;
  const bool complete = !worker_busy && Avida().IsComplete();
  restart_button.SetDisabled(!run_started || run_mode != RunMode::PAUSED || worker_busy);
  step_button.SetDisabled(complete || run_mode != RunMode::PAUSED || worker_busy);
  play_button.SetDisabled(complete);
  pause_button.SetDisabled(complete || run_mode == RunMode::PAUSED);
  fast_forward_button.SetDisabled(complete);
  color_selector.Disabled(complete || population_running);
  org_stats_mode.SetDisabled(population_running);
  freezer_mode.SetDisabled(population_running);
  configure_mode.SetDisabled(population_running);

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
  save_organism_button.SetDisabled(population_running || !GetActiveOrganism());
  save_configuration_button.SetDisabled(population_running);
  save_run_button.SetDisabled(!run_started || population_running);
}

void AvidaWebApp::SetRunMode(RunMode new_mode) {
  if (new_mode != RunMode::PAUSED && !run_started) StartRun();
  const bool worker_busy = SimulationWorkerBusy();
  if (!worker_busy
      && new_mode != RunMode::PAUSED
      && Avida().ConsumePauseRequest()) {
    new_mode = RunMode::PAUSED;
  }
  if (!worker_busy && Avida().IsComplete()) new_mode = RunMode::PAUSED;
  run_mode = new_mode;
  simulation_pause_requested.store(
    run_mode == RunMode::PAUSED,
    std::memory_order_release
  );
  play_elapsed_ms = 0.0;

  // Update the button state before Start(), which immediately invokes the
  // animation callback and may spend a long time in fast-forward mode.
  UpdateControls();
  if (!worker_busy) RefreshReadouts();

  if (run_mode == RunMode::PAUSED) {
    // The driver-owned update must reach its boundary before the animation can stop.
    if (!worker_busy && animation.GetActive()) animation.Stop();
  } else if (!animation.GetActive()) {
    animation.Start();
  }
}

void AvidaWebApp::ToggleRunMode(RunMode mode) {
  SetRunMode(run_mode == mode ? RunMode::PAUSED : mode);
}

void AvidaWebApp::FinishUpdate(bool can_continue,
                  bool redraw_population,
                  bool pause_requested) {
  if (!can_continue && Avida().IsExitPending()) {
    CaptureFinalView();
    Avida().Shutdown();
    redraw_population = false;
  }
  if (redraw_population && active_color_scale != ColorScale::BLANK) DrawPopulation();
  RefreshReadouts();
  pause_requested = Avida().ConsumePauseRequest() || pause_requested;
  if (!can_continue || pause_requested) SetRunMode(RunMode::PAUSED);
}

void AvidaWebApp::StepPopulation() {
  if (run_mode != RunMode::PAUSED) SetRunMode(RunMode::PAUSED);
  if (SimulationWorkerBusy() || Avida().IsComplete()) return;
  if (!run_started) StartRun();
  (void) Avida().ConsumePauseRequest();  // An explicit step advances past a start-time pause.
  FinishUpdate(Avida().AdvanceUpdate(), true);
  UpdateControls();
}

std::map<emp::String, emp::String> AvidaWebApp::SnapshotSettingValues() const {
  std::map<emp::String, emp::String> values;
  const auto & settings = Avida().GetSettings();
  for (const emp::String & name : settings.GetSettingNames()) {
    values.emplace(name, settings.Get<emp::String>(name));
  }
  return values;
}

void AvidaWebApp::CreateConfiguredAvida(
  const std::map<emp::String, emp::String> & values,
  const emp::String & ancestor_genome
) {
  emp_assert(!SimulationWorkerBusy());
  if (animation.GetActive()) animation.Stop();
  if (avida) {
    Avida().GetPlugIn<WebInterfaceBridge>().SetOnStartCallback({});
    Avida().GetPlugIn<WebInterfaceBridge>().SetBeforeExitCallback({});
    avida.reset();
  }

  avida = std::make_unique<avida_t>();
  auto & settings = Avida().GetSettings();
  settings.Set("base.config_dir", std::string{"/config"});
  settings.Set("base.data_dir", std::string{"/data"});
  settings.Load(population_adapter_t::CONFIG_PATH);
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
  configured_ancestor_genome = ancestor_genome;
  if (configured_ancestor_genome.size()) {
    static constexpr const char * frozen_ancestor_path = "/tmp/avida-web-frozen-ancestor.org";
    std::ofstream ancestor_output{frozen_ancestor_path};
    ancestor_output << configured_ancestor_genome;
    settings.Set("base.ancestor_filename", emp::String{frozen_ancestor_path});
  }

  run_mode = RunMode::PAUSED;
  run_started = false;
  has_final_snapshot = false;
  last_grid_redraw_update = 0;
  play_elapsed_ms = 0.0;
  population_pixels.clear();
  continuous_values.clear();
  final_statistic_values.clear();
  final_color_legend_html.clear();
  active_organism = {};
  active_cell_id = avida_web::EMPTY_CELL;
  CollectPopulationViewOptions();
}

void AvidaWebApp::StartRun() {
  emp_assert(!run_started);
  ApplyStructuredConfiguration();
  Avida().GetPlugIn<DriverBuffered>().SetInjectAncestor(placed_organisms.empty());
  Avida().GetPlugIn<WebInterfaceBridge>().SetOnStartCallback(
    [this](){ ApplyPlacedOrganisms(); }
  );
  Avida().GetPlugIn<WebInterfaceBridge>().SetBeforeExitCallback({});
  Avida().InitializePaused();
  run_started = true;
  CollectPopulationViewOptions();
  RequestInterfaceRebuild();
}

void AvidaWebApp::RestartPopulation() {
  if (!ConfirmPopulationRestart()) return;
  SetRunMode(RunMode::PAUSED);
  if (SimulationWorkerBusy()) return;
  const auto current_values = SnapshotSettingValues();
  const emp::String current_ancestor = configured_ancestor_genome;
  CreateConfiguredAvida(current_values, current_ancestor);
  RequestInterfaceRebuild();
}

void AvidaWebApp::OnAnimationFrame(const UI::Animate & frame) {
  if (active_application_mode == ApplicationMode::ORGANISM) {
    if (organism_run_mode == OrganismRunMode::PAUSED) return;
    organism_play_elapsed_ms += frame.GetStepTime();
    const double interval = organism_run_mode == OrganismRunMode::PLAY
      ? ORGANISM_PLAY_INTERVAL_MS
      : ORGANISM_FAST_FORWARD_INTERVAL_MS;
    if (organism_play_elapsed_ms < interval) return;

    organism_play_elapsed_ms = 0.0;  // Do not catch up after a delayed frame.
    AdvancePlayingOrganismInstruction();
    return;
  }

  if (auto completed = ConsumeSimulationUpdate()) {
    const bool can_continue = *completed;
    const bool pause_requested = simulation_pause_requested.load(std::memory_order_acquire)
      || Avida().ConsumePauseRequest()
      || run_mode == RunMode::PAUSED;
    const bool publish_update = pause_requested
      || !can_continue
      || run_mode == RunMode::PLAY
      || Avida().GetUpdate() - last_grid_redraw_update >= FAST_FORWARD_REDRAW_UPDATES;
    if (publish_update) {
      const bool redraw_population = active_color_scale != ColorScale::BLANK;
      FinishUpdate(can_continue, redraw_population, pause_requested);
      if (active_color_scale == ColorScale::BLANK) {
        last_grid_redraw_update = Avida().GetUpdate();
      }
      // FinishUpdate() refreshes controls whenever the run actually pauses or completes.
      // Replacing unchanged buttons after every worker batch breaks hover state and can discard
      // a pointer-down before its matching click event arrives.
    }

    if (pending_checkpoint_import && run_mode == RunMode::PAUSED) {
      std::string checkpoint = std::move(*pending_checkpoint_import);
      pending_checkpoint_import.reset();
      interface_rebuild_requested = false;
      ImportCheckpointFile(checkpoint);
      return;
    }
    if (interface_rebuild_requested) {
      interface_rebuild_requested = false;
      RequestInterfaceRebuild();
    }
  }

  if (run_mode == RunMode::PAUSED) {
    if (!SimulationWorkerBusy() && animation.GetActive()) animation.Stop();
    return;
  }

  if (SimulationWorkerBusy()) return;
  if (run_mode == RunMode::PLAY) {
    play_elapsed_ms += frame.GetStepTime();
    if (play_elapsed_ms < PLAY_INTERVAL_MS) return;
    play_elapsed_ms = 0.0;  // Do not catch up after a delayed or backgrounded frame.
  }
  const size_t update_budget = run_mode == RunMode::FAST_FORWARD
    ? FAST_FORWARD_WORK_BATCH_UPDATES
    : 1;
  (void) DispatchSimulationUpdate(update_budget);
}

void AvidaWebApp::Initialize() {
  InstallCheckpointWarningCapture();
  CreateConfiguredAvida();
  StartSimulationWorker();
  default_setting_values = SnapshotSettingValues();
  InitializeFreezer();
  InitializeDragCallbacks();
  InstallOrganismDragBridge(drop_organism_callback_id, freeze_grid_callback_id);
  InstallPopulationKeyboardBridge(
    population_step_callback_id, population_fast_forward_callback_id
  );
  InstallOrganismKeyboardBridge(organism_space_step_callback_id);
  InstallOrganismModeInteractionBridge(
    organism_scrub_callback_id, organism_head_callback_id
  );
  std::println(
    "Loaded {} (substitution probability = {}).",
    population_adapter_t::CONFIG_PATH,
    Avida().GetSettings().Get<double>("mutations.substitution_prob")
  );
  RebuildInterface();
}
