#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

void AvidaWebApp::RequestInterfaceRebuild() {
  if (SimulationWorkerBusy()) {
    interface_rebuild_requested = true;
    return;
  }
  if (interface_rebuild_requested) return;
  interface_rebuild_requested = true;
  emp::DelayCall([this](){
    if (!interface_rebuild_requested) return;
    if (SimulationWorkerBusy()) return;
    interface_rebuild_requested = false;
    RebuildInterface();
  }, 0);
}

void AvidaWebApp::SetSidePanel(SidePanel panel) {
  active_side_panel = panel;
  const bool show_population = panel == SidePanel::POPULATION;
  const bool show_organism = panel == SidePanel::ORGANISM;
  const bool show_freezer = panel == SidePanel::FREEZER;
  const bool show_configuration = panel == SidePanel::CONFIGURATION;

  run_inspector.SetCSS("display", show_population ? "block" : "none");
  org_stats_inspector.SetCSS("display", show_organism ? "block" : "none");
  freezer_inspector.SetCSS("display", show_freezer ? "block" : "none");
  configuration_inspector.SetCSS("display", show_configuration ? "block" : "none");
  pop_stats_mode.SetAttr(
    "class", show_population
      ? "mode-button side-mode-button is-active" : "mode-button side-mode-button"
  );
  org_stats_mode.SetAttr(
    "class", show_organism
      ? "mode-button side-mode-button is-active" : "mode-button side-mode-button"
  );
  freezer_mode.SetAttr(
    "class", show_freezer
      ? "mode-button side-mode-button is-active" : "mode-button side-mode-button"
  );
  configure_mode.SetAttr(
    "class", show_configuration
      ? "mode-button side-mode-button is-active" : "mode-button side-mode-button"
  );
  pop_stats_mode.SetAttr("aria-pressed", show_population ? "true" : "false");
  org_stats_mode.SetAttr("aria-pressed", show_organism ? "true" : "false");
  freezer_mode.SetAttr("aria-pressed", show_freezer ? "true" : "false");
  configure_mode.SetAttr("aria-pressed", show_configuration ? "true" : "false");
  if (show_organism) org_stats_content.Redraw();
}

void AvidaWebApp::BuildInterface() {
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

  UI::Div product_identity{"product_identity"};
  product_identity.AddAttr("class", "product-identity");
  product_identity <<
    "<p class='product-title'>The Avida Digital Evolution Research Platform</p>"
    "<p class='product-version'>version 5.0.0 alpha</p>";

  UI::Div modes{"mode_buttons"};
  modes.AddAttr("class", "mode-buttons");
  UI::Button population_mode{
    [this](){ SetApplicationMode(ApplicationMode::POPULATION); },
    "<img src='assets/icons/PopGrid.png' alt=''><span>POPULATION</span>",
    "population_mode"
  };
  UI::Button organism_mode{
    [this](){ SetApplicationMode(ApplicationMode::ORGANISM); },
    "<img src='assets/icons/ModeOrganism.png' alt=''><span>ORGANISMS</span>",
    "organism_mode"
  };
  UI::Button analyze_mode{
    [](){},
    "<img src='assets/icons/ModeAnalyze.png' alt=''><span>ANALYZE</span>",
    "analyze_mode"
  };
  const bool population_active = active_application_mode == ApplicationMode::POPULATION;
  population_mode.AddAttr("class", population_active ? "mode-button is-active" : "mode-button");
  organism_mode.AddAttr("class", population_active ? "mode-button" : "mode-button is-active");
  analyze_mode.AddAttr("class", "mode-button");
  population_mode.SetAttr("aria-label", "Population Mode");
  organism_mode.SetAttr("aria-label", "Organism Mode");
  analyze_mode.SetAttr("aria-label", "Analyze Mode");
  population_mode.SetAttr("aria-pressed", population_active ? "true" : "false");
  organism_mode.SetAttr("aria-pressed", population_active ? "false" : "true");
  analyze_mode.SetAttr("aria-pressed", "false");
  population_mode.SetTitle("Population Mode");
  organism_mode.SetTitle("Organism Mode");
  analyze_mode.SetTitle("Analyze Mode");
  modes << population_mode;
  modes << organism_mode;
  modes << analyze_mode;

  UI::Div side_modes{"side_mode_buttons"};
  side_modes.AddAttr("class", "mode-buttons side-mode-buttons");
  if (!population_active) side_modes.SetCSS("visibility", "hidden");
  pop_stats_mode = UI::Button{
    [this](){ SetSidePanel(SidePanel::POPULATION); },
    "<img src='assets/icons/StatsPop.png' alt=''><span>POP STATS</span>",
    "pop_stats_mode"
  };
  org_stats_mode = UI::Button{
    [this](){ SetSidePanel(SidePanel::ORGANISM); },
    "<img src='assets/icons/StatsOrg.png' alt=''><span>ORG STATS</span>",
    "org_stats_mode"
  };
  freezer_mode = UI::Button{
    [this](){ SetSidePanel(SidePanel::FREEZER); },
    "<img src='assets/icons/Freezer.png' alt=''><span>FREEZER</span>",
    "freezer_mode"
  };
  configure_mode = UI::Button{
    [this](){
      SetSidePanel(
        active_side_panel == SidePanel::CONFIGURATION
          ? SidePanel::POPULATION
          : SidePanel::CONFIGURATION
      );
    },
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
  org_stats_mode.SetAttr("aria-controls", "org_stats_inspector");
  freezer_mode.SetAttr("aria-controls", "freezer_inspector");
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
  primary_header << product_identity;
  primary_header << modes;
  header << primary_header;
  header << side_modes;
  app << header;

  if (!population_active) {
    BuildOrganismModeWorkspace(app);
    document << app;
    return;
  }

  UI::Div workspace{"workspace"};
  workspace.AddAttr("class", "workspace");
  UI::Div population_card{"population_card"};
  population_card.AddAttr("class", "population-card");
  population_card << emp::MakeString(
    "<p class='population-description'>", population_adapter_t::DESCRIPTION, "</p>"
  );

  UI::Div canvas_frame{"canvas_frame"};
  canvas_frame.AddAttr("class", "canvas-frame");
  UI::Canvas population_canvas{
    static_cast<double>(PopulationWidth()),
    static_cast<double>(PopulationHeight()),
    "population_canvas"
  };
  population_canvas.AddAttr("class", "population-canvas");
  population_canvas.SetAttr("role", "img");
  population_canvas.SetAttr("aria-label", population_adapter_t::DESCRIPTION);
  population_canvas.SetAttr("data-grid-width", PopulationWidth());
  population_canvas.SetAttr("data-grid-height", PopulationHeight());
  population_canvas.SetTitle(population_adapter_t::PLACEMENT_HELP);
  population_canvas.SetAttr("oncontextmenu", "event.preventDefault();");
  population_canvas.OnClick(std::function<void(UI::MouseEvent)>{
    [this](UI::MouseEvent event) {
      const int cell_id = GetPopulationCellAtClient(
        event.clientX,
        event.clientY,
        static_cast<int>(PopulationWidth()),
        static_cast<int>(PopulationHeight())
      );
      if (cell_id < 0) return;
      if (event.ctrlKey) {
        OpenGridCellMenu(
          static_cast<size_t>(cell_id), event.clientX, event.clientY
        );
      } else {
        SelectPopulationCell(static_cast<size_t>(cell_id));
      }
    }
  });
  population_canvas.On("contextmenu", std::function<void(UI::MouseEvent)>{
    [this](UI::MouseEvent event) {
      const int cell_id = GetPopulationCellAtClient(
        event.clientX,
        event.clientY,
        static_cast<int>(PopulationWidth()),
        static_cast<int>(PopulationHeight())
      );
      if (cell_id >= 0) {
        OpenGridCellMenu(
          static_cast<size_t>(cell_id), event.clientX, event.clientY
        );
      }
    }
  });
  UI::Div population_surface{"population_grid_surface"};
  population_surface.AddAttr("class", "population-grid-surface");
  population_surface.AddAttr(
    "style",
    emp::MakeString(
      "--grid-cell-width: ", 100.0 / PopulationWidth(),
      "%; --grid-cell-height: ", 100.0 / PopulationHeight(), "%;"
    )
  );
  population_surface << population_canvas;
  UI::Div active_cell_highlight{"active_cell_highlight"};
  active_cell_highlight.AddAttr("class", "active-cell-highlight");
  active_cell_highlight.SetAttr("aria-hidden", "true");
  population_surface << active_cell_highlight;
  UI::Div grid_cell_menu{"grid_cell_menu"};
  grid_cell_menu.AddAttr("class", "grid-cell-menu");
  grid_cell_menu.SetAttr("role", "menu");
  grid_cell_menu.SetAttr("aria-label", "Organism actions");
  grid_cell_menu.SetCSS("display", "none");
  UI::Button save_grid_organism_button{
    [this](){
      HideGridCellMenu();
      SaveGridCellOrganism(grid_context_cell_id);
    },
    "Save Organism",
    "save_grid_organism_button"
  };
  save_grid_organism_button.AddAttr("class", "grid-cell-menu-action");
  save_grid_organism_button.SetAttr("role", "menuitem");
  UI::Button remove_grid_organism_button{
    [this](){
      HideGridCellMenu();
      RemoveGridCellOrganism(grid_context_cell_id);
    },
    "Remove Organism",
    "remove_grid_organism_button"
  };
  remove_grid_organism_button.AddAttr(
    "class", "grid-cell-menu-action grid-cell-menu-remove"
  );
  remove_grid_organism_button.SetAttr("role", "menuitem");
  grid_cell_menu << save_grid_organism_button;
  grid_cell_menu << remove_grid_organism_button;
  population_surface << grid_cell_menu;
  if (!run_started && placed_organisms.size()) {
    population_surface << emp::MakeString(
      "<div class='staged-organism-badge'>",
      placed_organisms.size(),
      placed_organisms.size() == 1 ? " organism staged" : " organisms staged",
      "</div>"
    );
  }
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
    [this](){ ToggleRunMode(RunMode::PLAY); }, "&#x25B6;", "play_button"
  );
  pause_button = UI::Button(
    [this](){ SetRunMode(RunMode::PAUSED); }, "&#x275A;&#x275A;", "pause_button"
  );
  fast_forward_button = UI::Button(
    [this](){ ToggleRunMode(RunMode::FAST_FORWARD); },
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
  step_button.SetTitle("Step (Space)");
  play_button.SetTitle("Play");
  pause_button.SetTitle("Pause");
  fast_forward_button.SetTitle("Fast-forward (>)");
  color_selector.AddAttr("class", "color-selector");
  transport_buttons << restart_button;
  transport_buttons << step_button;
  transport_buttons << play_button;
  transport_buttons << fast_forward_button;
  transport_buttons << pause_button;
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
  population_color_legend = UI::Text{"population_color_legend"};
  population_color_legend << UI::Live([this](){
    return BuildPopulationColorLegendHTML();
  });
  run_inspector << population_color_legend;

  org_stats_inspector = UI::Div{"org_stats_inspector"};
  org_stats_inspector.AddAttr("class", "org-stats-inspector");
  org_stats_inspector << "<h2>Organism</h2>";
  org_stats_content = UI::Text{"org_stats_content"};
  org_stats_content << UI::Live([this](){ return BuildOrganismStatsHTML(); });
  org_stats_inspector << org_stats_content;

  BuildFreezerPanel();
  BuildConfigurationPanel();

  workspace << population_card;
  UI::Div side_panel{"side_panel"};
  side_panel.AddAttr("class", "side-panel");
  side_panel << run_inspector;
  side_panel << org_stats_inspector;
  side_panel << freezer_inspector;
  side_panel << configuration_inspector;
  workspace << side_panel;
  app << workspace;
  document << app;
}

void AvidaWebApp::RebuildInterface() {
  const SidePanel restore_side_panel = active_side_panel;
  document.Freeze();
  document.ClearChildren();
  configuration_inputs.clear();
  configuration_selectors.clear();
  statistic_texts.clear();
  SetupColorSelector();
  BuildInterface();
  document.Activate();
  if (active_application_mode == ApplicationMode::POPULATION) {
    SetSidePanel(restore_side_panel);
    UpdateControls();
    DrawPopulation();
    RefreshReadouts();
  } else {
    UpdateOrganismModeControls();
    FocusTrackedOrganismHead();
  }
}
