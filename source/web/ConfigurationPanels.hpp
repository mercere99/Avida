#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

void AvidaWebApp::AddConfigurationSetting(UI::Div & scope_panel,
                             const emp::String & setting_name,
                             const emp::String & local_name,
                             size_t setting_id) {
  const auto & settings = Avida().GetSettings();
  const auto & metadata = settings.Metadata(setting_name);
  const emp::String current_value = GetDisplaySettingValue(setting_name);
  const emp::String raw_value = settings.Get<emp::String>(setting_name);
  const std::string type_name = settings.GetTypeName(setting_name);
  const bool is_integer = type_name == "int64_t" || type_name == "uint64_t"
    || type_name == "int32_t" || type_name == "uint32_t";
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

void AvidaWebApp::BuildSettingsConfiguration(UI::Div & content) {
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

void AvidaWebApp::BuildEnvironmentConfiguration(UI::Div & content) {
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

void AvidaWebApp::BuildEventsConfiguration(UI::Div & content) {
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

void AvidaWebApp::BuildConfigurationPanel() {
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
