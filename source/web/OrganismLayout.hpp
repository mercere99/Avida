#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

void AvidaWebApp::BuildOrganismModeWorkspace(UI::Div & app) {
  UI::Div workspace{"organism_mode_workspace"};
  workspace.AddAttr("class", "organism-mode-workspace");

  UI::Div toolbar{"organism_mode_toolbar"};
  toolbar.AddAttr("class", "organism-mode-toolbar");
  UI::Div toolbar_actions{"organism_mode_toolbar_actions"};
  toolbar_actions.AddAttr("class", "organism-mode-toolbar-actions");
  organism_reset_button = UI::Button{
    [this](){ ResetOrganismAnalysis(); },
    "<span class='restart-icon' aria-hidden='true'></span>",
    "organism_reset_button"
  };
  organism_step_button = UI::Button{
    [this](){ StepOrganismInstruction(); },
    "<span class='step-icon' aria-hidden='true'></span>",
    "organism_step_button"
  };
  organism_play_button = UI::Button{
    [this](){ ToggleOrganismRunMode(OrganismRunMode::PLAY); },
    "&#x25B6;",
    "organism_play_button"
  };
  organism_pause_button = UI::Button{
    [this](){ SetOrganismRunMode(OrganismRunMode::PAUSED); },
    "&#x275A;&#x275A;",
    "organism_pause_button"
  };
  organism_fast_forward_button = UI::Button{
    [this](){ ToggleOrganismRunMode(OrganismRunMode::FAST_FORWARD); },
    "&#x25B6;&#x25B6;",
    "organism_fast_forward_button"
  };
  organism_reset_button.AddAttr("class", "transport-button icon-button restart-button");
  organism_step_button.AddAttr("class", "transport-button icon-button");
  organism_play_button.AddAttr("class", "transport-button icon-button");
  organism_pause_button.AddAttr("class", "transport-button icon-button is-active");
  organism_fast_forward_button.AddAttr("class", "transport-button icon-button");
  organism_reset_button.SetAttr("aria-label", "Rewind organism execution");
  organism_step_button.SetAttr("aria-label", "Execute one organism instruction");
  organism_play_button.SetAttr("aria-label", "Play at two instructions per second");
  organism_pause_button.SetAttr("aria-label", "Pause organism execution");
  organism_fast_forward_button.SetAttr(
    "aria-label", "Fast-forward at twenty instructions per second"
  );
  organism_reset_button.SetTitle("Rewind");
  organism_step_button.SetTitle("Step (Space)");
  organism_play_button.SetTitle("Play (2 instructions per second)");
  organism_pause_button.SetTitle("Pause");
  organism_fast_forward_button.SetTitle("Fast-forward (20 instructions per second)");
  organism_offspring_button = UI::Button{
    [this](){ ViewOrganismOffspring(); },
    "View offspring &#x2192;",
    "organism_offspring_button"
  };
  organism_offspring_button.AddAttr("class", "organism-toolbar-button offspring");

  UI::Div freezer_picker{"organism_freezer_picker"};
  freezer_picker.AddAttr("class", "organism-freezer-picker");
  freezer_picker << "<label for='organism_freezer_selector'>Organism</label>";
  organism_freezer_selector = UI::Selector{"organism_freezer_selector"};
  organism_freezer_selector.SetOption("Choose from freezer…", [](){});
  for (const auto & item : freezer.organisms) {
    organism_freezer_selector.SetOption(
      emp::MakeWebSafe(item.name),
      [this, id=item.id](){ SelectFrozenOrganismForAnalysis(id); }
    );
  }
  organism_freezer_selector.SelectID(0);
  organism_freezer_selector.Disabled(freezer.organisms.empty());
  organism_freezer_selector.AddAttr("class", "organism-freezer-selector");
  organism_freezer_selector.SetAttr("aria-label", "Choose an organism from the freezer");
  freezer_picker << organism_freezer_selector;
  toolbar_actions << freezer_picker;

  UI::Div organism_transport_buttons{"organism_transport_buttons"};
  organism_transport_buttons.AddAttr("class", "organism-transport-buttons");
  organism_transport_buttons << organism_reset_button;
  organism_transport_buttons << organism_step_button;
  organism_transport_buttons << organism_play_button;
  organism_transport_buttons << organism_fast_forward_button;
  organism_transport_buttons << organism_pause_button;
  toolbar_actions << organism_transport_buttons;
  toolbar_actions << organism_offspring_button;
  toolbar << toolbar_actions;

  UI::Div timeline{"organism_timeline"};
  timeline.AddAttr("class", "organism-timeline");
  timeline << "<label for='organism_position_slider'>Execution position</label>";
  organism_position_slider = UI::Input{
    [](std::string){},
    "range", "", "organism_position_slider"
  };
  organism_position_slider.Min("0");
  organism_position_slider.Max(emp::MakeString(organism_execution_length));
  organism_position_slider.Step("1");
  organism_position_slider.Value(emp::MakeString(organism_execution_step));
  organism_position_slider.AddAttr("class", "organism-position-slider");
  organism_position_slider.SetAttr("aria-label", "Organism execution position");
  UI::Div slider_wrap{"organism_timeline_slider_wrap"};
  slider_wrap.AddAttr("class", "organism-timeline-slider-wrap");
  slider_wrap << organism_position_slider;
  slider_wrap << "<div id='organism_task_ticks' class='organism-task-ticks' "
                 "aria-hidden='true'></div>";
  timeline << slider_wrap;
  timeline << emp::MakeString(
    "<output id='organism_position_readout' for='organism_position_slider'>",
    organism_execution_step, " / ", organism_execution_length, "</output>"
  );
  toolbar << timeline;
  workspace << toolbar;

  organism_mode_content = UI::Text{"organism_mode_content"};
  organism_mode_content << UI::Live([this](){ return BuildOrganismModeHTML(); });
  workspace << organism_mode_content;
  app << workspace;
  UpdateOrganismModeControls();
}
