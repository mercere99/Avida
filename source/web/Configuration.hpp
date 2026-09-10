#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

void AvidaWebApp::ApplyReactionConfiguration() {
  emp_assert(!run_started);
  Reactions().SetConfigs(reaction_configs);
  CollectPopulationViewOptions();
}

void AvidaWebApp::ApplyEventConfiguration() {
  Events().SetConfigs(event_configs);
}

void AvidaWebApp::ApplyStructuredConfiguration() {
  ApplyReactionConfiguration();
  ApplyEventConfiguration();
}

emp::String AvidaWebApp::HumanizeSettingName(emp::String name) {
  for (char & character : name) if (character == '_') character = ' ';
  if (name.size()) name[0] = static_cast<char>(std::toupper(name[0]));
  return name;
}

emp::String AvidaWebApp::FormatFixedPoint(double value) {
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

emp::String AvidaWebApp::GetDisplaySettingValue(const emp::String & setting_name) const {
  const auto & settings = Avida().GetSettings();
  if (settings.GetTypeName(setting_name) == "double") {
    return FormatFixedPoint(settings.Get<double>(setting_name));
  }
  return settings.Get<emp::String>(setting_name);
}

bool AvidaWebApp::ShouldShowSetting(const emp::String & setting_name) const {
  const auto & metadata = Avida().GetSettings().Metadata(setting_name);
  if (metadata.HasTag("local only")) return false;
  if (metadata.HasTag("advanced") && !advanced_settings_visible) return false;
  return true;
}

void AvidaWebApp::ToggleAdvancedSettings() {
  advanced_settings_visible = !advanced_settings_visible;
  RequestInterfaceRebuild();
}

void AvidaWebApp::SetConfigurationTab(ConfigurationTab tab) {
  if (active_configuration_tab == tab) return;
  active_configuration_tab = tab;
  RequestInterfaceRebuild();
}

void AvidaWebApp::ResetConfiguration() {
  if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED) return;
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

void AvidaWebApp::AddReaction() {
  if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED || run_started) return;
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

void AvidaWebApp::RemoveReaction(size_t reaction_id) {
  if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED
      || run_started || reaction_id >= reaction_configs.size()) return;
  reaction_configs.erase(reaction_configs.begin() + reaction_id);
  ApplyReactionConfiguration();
  RequestInterfaceRebuild();
}

void AvidaWebApp::AddEvent() {
  if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED) return;
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

void AvidaWebApp::RemoveEvent(size_t event_id) {
  if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED
      || event_id >= event_configs.size()) return;
  event_configs.erase(event_configs.begin() + event_id);
  ApplyEventConfiguration();
  RequestInterfaceRebuild();
}

void AvidaWebApp::SetConfigurationValue(const emp::String & setting_name,
                           const std::string & value,
                           const emp::String & linked_control_id) {
  if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED || value.empty()) return;
  Avida().GetSettings().Set(setting_name, emp::String{value});

  if (linked_control_id.size()) {
    const emp::String current_value = GetDisplaySettingValue(setting_name);
    SetConfigurationControlValue(linked_control_id.c_str(), current_value.c_str());
  }

  if (!run_started && population_adapter_t::AffectsLayout(setting_name)) {
    DrawPopulation();
  }
}
