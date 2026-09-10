#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

avida_checkpoint::Compatibility AvidaWebApp::CheckpointCompatibility() {
  std::string module_pack;
  Avida().TriggerSignal([&module_pack](auto & module) {
    if (!module_pack.empty()) module_pack += '|';
    module_pack += module.GetName();
  });
  return {
    .avida_version = "5.0.0-alpha",
    .module_pack = std::move(module_pack),
    .cpu_profile = organism_adapter_t::CPU_PROFILE,
    .instruction_set = organism_adapter_t::INSTRUCTION_PROFILE,
    .population_structure = population_adapter_t::CHECKPOINT_PROFILE,
    .configuration_schema = CHECKPOINT_CONFIGURATION_SCHEMA
  };
}

std::string AvidaWebApp::SerializeConfiguration(
  FrozenConfiguration configuration
) {
  std::ostringstream output;
  emp::SerialPod pod{output};
  pod(configuration);
  return output.str();
}

std::string AvidaWebApp::BuildCheckpointFile(
  FrozenConfiguration configuration,
  const std::string & state
) {
  return avida_checkpoint::Build(
    CheckpointCompatibility(), SerializeConfiguration(std::move(configuration)), state
  );
}

void AvidaWebApp::SaveRun() {
  if (!run_started) return;

  SetRunMode(RunMode::PAUSED);
  if (SimulationWorkerBusy()) {
    freezer_message = "Pausing at the next update boundary; save again when Pause is ready.";
    RequestInterfaceRebuild();
    return;
  }
  if (!Avida().IsCheckpointSafe()) {
    freezer_message = "Checkpoint not saved: the run is not at a safe paused boundary.";
    RequestInterfaceRebuild();
    return;
  }

  std::ostringstream output;
  emp::SerialPod pod{output};
  pod(Avida());
  const std::string checkpoint = BuildCheckpointFile(
    SnapshotConfiguration(), output.str()
  );
  auto checkpoint_contents = avida_checkpoint::Parse(checkpoint);
  FrozenConfiguration verification_configuration;
  if (!checkpoint_contents) {
    freezer_message = emp::MakeString(
      "Checkpoint not saved: internal envelope validation failed: ",
      checkpoint_contents.error()
    );
    RequestInterfaceRebuild();
    return;
  }
  auto verification = BuildCheckpointCandidate(
    *checkpoint_contents, verification_configuration
  );
  if (!verification) {
    freezer_message = emp::MakeString(
      "Checkpoint not saved: internal round-trip validation failed: ",
      verification.error()
    );
    RequestInterfaceRebuild();
    return;
  }
  const emp::String filename = emp::MakeString(
    "avida-update-", Avida().GetUpdate(), ".avida-checkpoint"
  );
  DownloadBrowserFile(
    filename.c_str(), "application/vnd.avida.checkpoint", checkpoint.data(), checkpoint.size()
  );
  freezer_message = emp::MakeString(
    "Downloaded ", filename, " (", checkpoint.size(), " bytes)."
  );
  RequestInterfaceRebuild();
}

std::expected<std::unique_ptr<avida_t>, std::string>
AvidaWebApp::BuildCheckpointCandidate(
  const avida_checkpoint::Contents & contents,
  FrozenConfiguration & configuration
) {
  if (auto decoded = DeserializeCheckpointPart(
        contents.configuration, configuration, "configuration"
      ); !decoded) return std::unexpected(decoded.error());

  auto candidate = std::make_unique<avida_t>();
  auto & settings = candidate->GetSettings();
  settings.Set("base.config_dir", std::string{"/config"});
  settings.Set("base.data_dir", std::string{"/data"});
  settings.Load(population_adapter_t::CONFIG_PATH);
  candidate->GetPlugIn<ReactionsManager>().SetConfigs(configuration.reactions);
  candidate->GetPlugIn<EventManager>().SetConfigs(configuration.events);
  for (const auto & [name, value] : configuration.settings) {
    if (settings.HasSetting(name)) settings.Set(name, value);
  }

  checkpoint_warning_message.clear();
  capture_checkpoint_warnings = true;
  auto decoded = DeserializeCheckpointPart(
    contents.payload, *candidate, "population payload"
  );
  capture_checkpoint_warnings = false;
  if (!decoded) return std::unexpected(decoded.error());
  if (!checkpoint_warning_message.empty()) {
    return std::unexpected(
      "Checkpoint payload is incompatible or corrupt: " + checkpoint_warning_message
    );
  }
  if (!candidate->IsPaused()) {
    return std::unexpected(
      "Checkpoint is not at a paused update boundary and cannot be resumed safely."
    );
  }
  candidate->AfterLoad();
  if (!candidate->LoadedStateOK()) {
    if (!candidate->OK()) {
      return std::unexpected("Checkpoint state failed Biota validation.");
    }
    std::string failed_module;
    candidate->TriggerSignal([&failed_module](auto & module) {
      if constexpr (requires { module.LoadedStateOK(); }) {
        if (failed_module.empty() && !module.LoadedStateOK()) failed_module = module.GetName();
      }
    });
    if (!failed_module.empty()) {
      return std::unexpected("Checkpoint state failed " + failed_module + " validation.");
    }
    return std::unexpected("Checkpoint state failed Avida cross-module validation.");
  }
  return candidate;
}

void AvidaWebApp::ImportCheckpointFile(const std::string & checkpoint) {
  if (SimulationWorkerBusy()) {
    pending_checkpoint_import = checkpoint;
    SetRunMode(RunMode::PAUSED);
    freezer_message = "Pausing at the next update boundary before loading the checkpoint.";
    RequestInterfaceRebuild();
    return;
  }
  pending_checkpoint_import.reset();
  auto contents = avida_checkpoint::Parse(checkpoint);
  if (!contents) {
    freezer_message = emp::MakeString("Checkpoint rejected: ", contents.error());
    RequestInterfaceRebuild();
    return;
  }
  if (auto compatible = avida_checkpoint::ValidateCompatibility(
        contents->compatibility, CheckpointCompatibility()
      ); !compatible) {
    freezer_message = emp::MakeString("Checkpoint rejected: ", compatible.error());
    RequestInterfaceRebuild();
    return;
  }

  FrozenConfiguration configuration;
  auto candidate = BuildCheckpointCandidate(*contents, configuration);
  if (!candidate) {
    freezer_message = emp::MakeString("Checkpoint rejected: ", candidate.error());
    RequestInterfaceRebuild();
    return;
  }

  SetRunMode(RunMode::PAUSED);
  if (animation.GetActive()) animation.Stop();
  if (avida) {
    Avida().GetPlugIn<WebInterfaceBridge>().SetOnStartCallback({});
    Avida().GetPlugIn<WebInterfaceBridge>().SetBeforeExitCallback({});
  }
  reaction_configs = configuration.reactions;
  event_configs = configuration.events;
  placed_organisms = configuration.placed_organisms;
  configured_ancestor_genome = configuration.ancestor_genome;
  avida = std::move(*candidate);
  Avida().GetPlugIn<WebInterfaceBridge>().SetBeforeExitCallback({});
  run_started = true;
  run_mode = RunMode::PAUSED;
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
  freezer_message = emp::MakeString(
    "Loaded checkpoint at update ", Avida().GetUpdate(), " (",
    Avida().GetNumOrgs(), " organisms)."
  );
  RequestInterfaceRebuild();
}
