#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

emp::String AvidaWebApp::ReadTextFile(const std::string & filename) {
  std::ifstream input{filename};
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

std::string AvidaWebApp::FreezerStorageKey() {
  const auto compatibility = CheckpointCompatibility();
  static constexpr std::string_view legacy_modules =
    "OrgTypeAvidian|PopGrid|DriverBuffered|MutationsDivideSub|TrackGeneration|"
    "TrackGenotypes|EventManager|EnvironmentLogic|ReactionsManager|TrackMetabolism|"
    "WebInterfaceBridge";
  if (compatibility.module_pack == legacy_modules) return "avida.web.freezer.v3";
  return "avida.web.freezer.v3." + compatibility.module_pack;
}

bool AvidaWebApp::PersistFreezer() {
  std::ostringstream output;
  output << "AVIDA_FREEZER_V3\n";
  emp::SerialPod pod{output};
  pod(freezer);
  const std::string serialized = output.str();
  const bool persisted = SaveFreezerLocalStorage(
    FreezerStorageKey().c_str(), serialized.c_str()
  );
  freezer_message = persisted
    ? "Saved in this browser."
    : "Saved for this session; browser storage is unavailable or full.";
  return persisted;
}

bool AvidaWebApp::RestoreFreezer() {
  if (char * stored_value = LoadFreezerLocalStorage(FreezerStorageKey().c_str())) {
    const std::string serialized{stored_value};
    std::free(stored_value);
    static constexpr std::string_view prefix{"AVIDA_FREEZER_V3\n"};
    if (serialized.starts_with(prefix)) {
      std::istringstream input{serialized.substr(prefix.size())};
      emp::SerialPod pod{input};
      pod(freezer);
      freezer_message = "Loaded organisms and configurations from this browser.";
      return true;
    }
  }

  return false;
}

AvidaWebApp::FrozenConfiguration AvidaWebApp::SnapshotConfiguration() const {
  return {
    .settings = SnapshotSettingValues(),
    .ancestor_genome = configured_ancestor_genome,
    .reactions = reaction_configs,
    .events = event_configs,
    .placed_organisms = placed_organisms
  };
}

void AvidaWebApp::InitializeFreezer() {
  if (RestoreFreezer()) return;

  const emp::String ancestor_text = ReadTextFile("/config/ancestor.org");
  size_t ancestor_size = 0;
  const auto ancestor = Organisms().LoadGenome("/config/ancestor.org");
  if (ancestor) ancestor_size = ancestor->size();
  freezer.organisms.push_back({
    .id = freezer.next_id++,
    .name = "Ancestor",
    .genome = ancestor_text,
    .instruction_count = ancestor_size
  });
  freezer.configurations.push_back({
    .id = freezer.next_id++,
    .name = "Default Configuration",
    .configuration = SnapshotConfiguration()
  });
  (void) PersistFreezer();
}

emp::String AvidaWebApp::TrimmedName(emp::String name) {
  while (name.size() && std::isspace(static_cast<unsigned char>(name.front()))) {
    name.erase(0, 1);
  }
  while (name.size() && std::isspace(static_cast<unsigned char>(name.back()))) {
    name.pop_back();
  }
  return name;
}

emp::String AvidaWebApp::DownloadStem(const emp::String & name) {
  emp::String stem;
  bool pending_dash = false;
  for (const unsigned char character : name) {
    if (std::isalnum(character) || character == '-' || character == '_') {
      if (pending_dash && stem.size()) stem += '-';
      stem += static_cast<char>(character);
      pending_dash = false;
    } else {
      pending_dash = true;
    }
  }
  if (!stem.size()) stem = "avida-item";
  return stem;
}

emp::String AvidaWebApp::QuoteConfigString(const emp::String & value) {
  emp::String result{'"'};
  for (const char character : value) {
    if (character == '\\' || character == '"') result += '\\';
    result += character;
  }
  result += '"';
  return result;
}

emp::String AvidaWebApp::BuildConfigurationFile(
  const FrozenConfiguration & configuration,
  const emp::String & ancestor_filename
) {
  std::ostringstream output;
  auto & settings = Avida().GetSettings();
  settings.Save(output, [&](const auto & info) -> emp::String {
    const emp::String & name = info.GetName();
    if (name == "base.config_dir") return QuoteConfigString(".");
    if (name == "base.data_dir") return QuoteConfigString("data");
    if (name == "base.ancestor_filename") return QuoteConfigString(ancestor_filename);

    const auto iterator = configuration.settings.find(name);
    if (iterator == configuration.settings.end()) return info.GetDefaultLiteral();
    if (settings.GetTypeName(name) == "emp::String") return QuoteConfigString(iterator->second);
    return iterator->second;
  });

  output << "# Environment reactions\n";
  for (const auto & reaction : configuration.reactions) {
    std::println(
      output,
      "Reaction {} {} {} {} {}",
      reaction.task_name,
      reaction.trait_name,
      reaction.operation,
      reaction.value,
      reaction.max_triggers
    );
  }

  output << "\n# Scheduled events\n";
  using Timing = EventManager<avida_t>::Timing;
  for (const auto & event : configuration.events) {
    if (event.timing == Timing::START) {
      std::println(output, "on start {}", event.command);
    } else if (event.timing == Timing::END) {
      std::println(output, "on end {}", event.command);
    } else if (event.timing == Timing::UPDATE) {
      std::println(output, "on update {} {}", event.start, event.command);
    } else if (event.stop) {
      std::println(
        output,
        "on update {}:{}:{} {}",
        event.start,
        event.interval,
        event.stop,
        event.command
      );
    } else {
      std::println(
        output,
        "on update {}:{} {}",
        event.start,
        event.interval,
        event.command
      );
    }
  }
  return output.str();
}

void AvidaWebApp::DownloadFrozenOrganism(size_t id) {
  for (const auto & item : freezer.organisms) {
    if (item.id != id) continue;
    const emp::String filename = DownloadStem(item.name) + ".org";
    DownloadBrowserFile(
      filename.c_str(), "text/plain;charset=utf-8", item.genome.data(), item.genome.size()
    );
    freezer_message = emp::MakeString("Downloaded ", filename, ".");
    return;
  }
}

void AvidaWebApp::DownloadFrozenConfiguration(size_t id) {
  for (const auto & item : freezer.configurations) {
    if (item.id != id) continue;
    const emp::String stem = DownloadStem(item.name);
    const emp::String config_filename = stem + ".cfg";
    const emp::String ancestor_filename = item.configuration.ancestor_genome.size()
      ? stem + ".org"
      : emp::String{"ancestor.org"};
    const emp::String contents = BuildConfigurationFile(
      item.configuration, ancestor_filename
    );
    DownloadBrowserFile(
      config_filename.c_str(), "text/plain;charset=utf-8", contents.data(), contents.size()
    );
    if (item.configuration.ancestor_genome.size()) {
      DownloadBrowserFile(
        ancestor_filename.c_str(),
        "text/plain;charset=utf-8",
        item.configuration.ancestor_genome.data(),
        item.configuration.ancestor_genome.size()
      );
    }
    freezer_message = emp::MakeString("Downloaded ", config_filename, ".");
    return;
  }
}

void AvidaWebApp::SaveActiveOrganism() {
  if (SimulationWorkerBusy()) return;
  const auto * organism = GetActiveOrganism();
  if (!organism) return;

  const auto & genome = organism->GetGenome();
  freezer.organisms.push_back({
    .id = freezer.next_id++,
    .name = emp::MakeString("Organism #", organism->GetGlobalID(), " at update ", Avida().GetUpdate()),
    .genome = GenomeFileText(*organism),
    .instruction_count = genome.size()
  });
  (void) PersistFreezer();
  RequestInterfaceRebuild();
}

void AvidaWebApp::SaveConfiguration() {
  if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED) return;
  const size_t id = freezer.next_id++;
  freezer.configurations.push_back({
    .id = id,
    .name = emp::MakeString("Configuration ", id),
    .configuration = SnapshotConfiguration()
  });
  (void) PersistFreezer();
  RequestInterfaceRebuild();
}

void AvidaWebApp::LoadConfiguration(const FrozenConfiguration & configuration) {
  SetRunMode(RunMode::PAUSED);
  if (SimulationWorkerBusy()) {
    freezer_message = "Pausing at the next update boundary; load again when Pause is ready.";
    RequestInterfaceRebuild();
    return;
  }
  reaction_configs = configuration.reactions;
  event_configs = configuration.events;
  placed_organisms = configuration.placed_organisms;
  CreateConfiguredAvida(configuration.settings, configuration.ancestor_genome);
  RequestInterfaceRebuild();
}

void AvidaWebApp::LoadFrozenOrganism(size_t id) {
  if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED) return;
  for (const auto & item : freezer.organisms) {
    if (item.id != id) continue;
    FrozenConfiguration configuration = SnapshotConfiguration();
    configuration.ancestor_genome = item.genome;
    configuration.placed_organisms.clear();
    LoadConfiguration(configuration);
    freezer_message = emp::MakeString("Loaded ", item.name, " as the dish ancestor.");
    return;
  }
}

void AvidaWebApp::LoadFrozenConfiguration(size_t id) {
  if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED) return;
  for (const auto & item : freezer.configurations) {
    if (item.id != id) continue;
    LoadConfiguration(item.configuration);
    freezer_message = emp::MakeString("Loaded ", item.name, ".");
    return;
  }
}
