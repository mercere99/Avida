/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2025 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <emscripten.h>

#include "emp/geometry/Physics2D.hpp"
#include "emp/web/Animate.hpp"
#include "emp/web/canvas_utils.hpp"
#include "emp/web/Document.hpp"
#include "emp/web/KeypressManager.hpp"
#include "emp/web/web.hpp"

#include "Avida.hpp"

namespace UI = emp::web;

class WorldView {
private:
  enum class ViewMode { NONE=0, GENOTYPES, FITNESS };
  ViewMode mode=ViewMode::GENOTYPES;

public:
  void SetView_None() { mode = ViewMode::NONE; }
  void SetView_Genotypes() { mode = ViewMode::GENOTYPES; }
  void SetView_Fitness() { mode = ViewMode::FITNESS; }
};

class AvidaWeb {
private:
  Avida avida{};

  emp::vector<emp::String> errors;

  UI::Animate anim;
  UI::Document doc{"emp_base"};
  UI::KeypressManager keypress_manager;

  // Set up the main window
  emp::Size2D main_win_size{600, 600};
  enum class ViewMain { NONE=0, WORLD, CONFIG, ORGANISM, GRAPH};
  ViewMain view_main=ViewMain::WORLD;

  WorldView world_view;

  // Sections of web page
  UI::Div intro_div{"intro_div"};      // Tabs along top
  UI::Div main_div{"main_div"};        // Display of main canvas
  UI::Div control_div{"control_div"};  // Pause / play / view / etc controllers (below main_div)
  UI::Div stats_div{"stats_div"};      // Quick stat info (upper right of main_div)
  UI::Div freezer_div{"freezer_div"};  // Save/Load organisms or populations (LR or main_div)

  // UI::Div settings_div{"settings_div"}; // Configure the current run
  // UI::Div graph_div{"graph_div"};       // Graph display of data
  // UI::Div organism_div{"organism_div"}; // Info about active organism

  // Styling
  UI::Style button_style {
    "padding", "10px 15px",
    "background-color", "#000066",   // Dark Blue background
    "color", "white",                // White text
    "border", "1px solid white",     // Thin white border
    "border-radius", "5px",          // Rounded corners
    "cursor", "pointer",
    "font-size", "16px",
    "transition", "background-color 0.3s ease, transform 0.3s ease" // Smooth transition
  }; 

  UI::Style table_style {
    "background-color", "white",
    "color", "white",
    "padding", "10px",
    "border", "1px solid black",
    "text_align", "center"
  };

  using key_event_t = emp::web::KeyboardEvent;

  // =====================  HELPER FUNCTIONS  =====================

  template <typename... Ts>
  void Error(size_t line_num, Ts &&... args) {
    errors.push_back(emp::MakeString("Error (line ", line_num, ") - ", args...));
  }

public:
  AvidaWeb() : anim([this](){ AvidaWeb::Animate(anim); }) {
    avida.Setup();

    // Link keypresses to the proper handlers
    keypress_manager.AddKeydownCallback([this](const key_event_t & evt){ return OnKeydown(evt); });

    // Add in the buttons along the top.
    doc << intro_div;

    // Add them main canvas to the interface.
    main_div << UI::Canvas(main_win_size, "view_main") << "<br>";
    doc << main_div;

    // Add grid control buttons.
    auto control_set = doc.AddDiv("buttons");
    // control_set.SetPosition(10, 260+world.height);
    control_set << UI::Button([this](){ DoStart(); }, "Start", "start_but");
    control_set << UI::Button([this](){ DoStep();  }, "Step", "step_but");
    control_set << UI::Button([this](){ DoReset(); }, "Reset", "reset_but");

    UI::Selector map_sel("map_sel");
    map_sel.SetOption("Blank Map (fast!)", [this](){ world_view.SetView_Genotypes(); });
    map_sel.SetOption("Genotype Map",      [this](){ world_view.SetView_Genotypes(); });
    map_sel.SetOption("Fitness Map",       [this](){ world_view.SetView_Fitness(); });
    map_sel.TriggerID(1);
    control_set << map_sel;

    // UI::Selector size_sel("size_sel");
    // size_sel.SetOption("Cell Size 2",  [this](){world.org_radius=2.0;} );
    // size_sel.SetOption("Cell Size 3",  [this](){world.org_radius=3.0;} );
    // size_sel.SetOption("Cell Size 4",  [this](){world.org_radius=4.0;} );
    // size_sel.SetOption("Cell Size 6",  [this](){world.org_radius=6.0;} );
    // size_sel.SetOption("Cell Size 8",  [this](){world.org_radius=8.0;} );
    // size_sel.SetOption("Cell Size 10", [this](){world.org_radius=10.0;} );
    // size_sel.SetOption("Cell Size 15", [this](){world.org_radius=15.0;} );
    // size_sel.TriggerID(2);
    // control_set << size_sel;

    // UI::Selector drift_sel("drift_sel");
    // drift_sel.SetOption("Flow Off",    [this](){world.drift=0.0;} );
    // drift_sel.SetOption("Flow Low",    [this](){world.drift=0.05;} );
    // drift_sel.SetOption("Flow Medium", [this](){world.drift=0.1;} );
    // drift_sel.SetOption("Flow High",   [this](){world.drift=0.15;} );
    // drift_sel.TriggerID(0);
    // control_set << drift_sel;


    // And stats (next to canvas)
    auto stats_set = doc.AddDiv("stats");
    stats_set.SetPosition(main_win_size.X() + 40, 80);

    stats_set << "Update: " << UI::Live( [this]() { return anim.GetFrameCount(); } ) << "<br>";
    stats_set << "Org Count: " << UI::Live( [this](){ return avida.GetPopulation("main").GetNumOrgs(); } ) << "<br>";

    // stats_set << "<br>"
    //           << "Sample Org: " << UI::Live( [this](){
    //                 return world.physics.NumBodies() ? world.physics.GetActiveBody().GetID() : 1000000;
    //               } ) << "<br>"
    //           << "Center: " << UI::Live( [this](){
    //                 return world.physics.NumBodies() ? world.physics.GetActiveBody().GetCenter() : emp::Point{-1,-1};
    //               } ) << "<br>"
    //           << "Links: " << UI::Live( [this](){
    //             return world.physics.NumBodies() ? world.physics.GetActiveBody().GetLinkIDs() : emp::vector<size_t>{};
    //           } ) << "<br>";

    stats_set << "<br>"
      "Each circle represents a <b>single cell</b>.<br>"
      "Cells are <b>attached</b> during gestation; in some setups, they may stay attached after birth.<br>"
      "<b>Colors</b> are (mostly) meaningless, but have a 5% chance of changing at birth.<br>"
      "<br>"
      "Press <b>Start/Stop</b> to begin or pause a run; <b>Step</b> advances a run by a single update.<br>"
      "Use <b>Reset</b> to restart a run from the beginning (using current settings).<br>"
      "Freeze the <b>Map</b> to speed up processing by more than 10-fold.<br>"
      "Cells can be <b>Individuals</b> or linked into clusters like <b>Snowflake</b> Yeast.<br>"
      "<b>Cell Sizes</b> can be changed, but you need to <b>Reset</b> the run to see the results.<br>"
      "<b>Flow</b> indicates the amount of Brownian motion in the run.<br>"
      "<b>Copy</b> rate determines how quickly cells should be reproducing.<br>"
      "<br>"
      "<b>Keyboard Shortcuts</b>:<br>"
      "&nbsp;&nbsp;<b>[SPACE]</b>: Start/Stop.<br>"
      "&nbsp;&nbsp;<b>[ARROWS]</b>: Move a cell around (forward and turns).<br>"
      "&nbsp;&nbsp;<b>[M]</b>: Toggle Map Mode (Basic vs. Blank).<br>"
      "&nbsp;&nbsp;<b>[R]</b>: Reset Run.<br>"
      "&nbsp;&nbsp;<b>[S]</b>: Step a single update.<br>"
      ;


    avida.Setup(); // Initialize the Avida population.

    // Draw initial state of the world.
    // UI::Draw( doc.Canvas("pop_view"), world.physics);
  }

  void Init() {
    doc << "<h1>Avida Test</h1>";
  }

  ~AvidaWeb() {}

  void Animate([[maybe_unused]] const UI::Animate & anim) {
    DEBUG_STACK();
    world.Update();

    switch (map_mode) {
    case MapMode::MAKE_BLANK:
      doc.Canvas("pop_view").Clear();
      map_mode = MapMode::BLANK;
    case MapMode::BLANK:
      break;
    case MapMode::BASIC:
      UI::Draw( doc.Canvas("pop_view"), world.physics);
      break;
    }

    doc.Div("stats").Redraw();
  }

  void DoStart() {
    DEBUG_STACK();
    anim.ToggleActive();
    auto start_but = doc.Button("start_but");
    auto step_but = doc.Button("step_but");

    if (anim.GetActive()) {
      start_but.SetLabel("Stop");    // If animation is running, button should read "Stop"
      step_but.SetDisabled(true);    // Cannot step animation already running.
    }
    else {
      start_but.SetLabel("Start");     // If animation is stopped, button should read "Start"
      step_but.SetDisabled(false);    // Can step stopped animation.
    }
  }

  void DoStep() {
    DEBUG_STACK();
    emp_assert(anim.GetActive() == false); // Step is only meaningful if the run is stopped.
    anim.Step();
  }

  void DoReset() {
    DEBUG_STACK();
    world.Reset();

    // Redraw the world.
    UI::Draw(doc.Canvas("pop_view"), world.physics);
  }


  bool OnKeydown(const emp::web::KeyboardEvent & evt_info) {
    DEBUG_STACK();
    // Reject most modified keypresses.
    if (evt_info.altKey || evt_info.ctrlKey || evt_info.metaKey) return false;

    const int key_code = evt_info.keyCode;
    bool return_value = true;
    auto & user_body = world.physics.GetActiveBody();

    switch (key_code) {
    case ' ':                                     // [SPACE] => Start / Stop a run
      DoStart();
      break;
    case 'M':                                     // M => Map Mode
      switch (map_mode) {
      case MapMode::BLANK:
      case MapMode::MAKE_BLANK:
        map_mode = MapMode::BASIC;
        break;
      case MapMode::BASIC:
        map_mode = MapMode::MAKE_BLANK;
        break;
      }
      break;
    case 'R':                                     // R => Reset population
      DoReset();
      break;
    case 'S':                                     // S => Step
      DoStep();
      break;

    // case 187:                                     // Plus  (grow)
    //   if (user_body) user_body->SetTargetRadius(user_body->GetTargetRadius() + 1);
    //   break;
    // case 189:                                     // Minus (shrink)
    //   if (user_body) {
    //     int body_size = user_body->GetTargetRadius();
    //     if (body_size > 1) user_body->SetTargetRadius(body_size - 1);
    //   }
    //   break;

    case 37:                                      // LEFT ARROW (Turn Left)
      if (user_body.IsActive()) user_body.RotateDegrees(45.0);
      break;
    case 38:                                      // UP ARROW (Accelerate)
      if (user_body.IsActive()) user_body.IncSpeed();
      break;
    case 39:                                      // RIGHT ARROW (Turn Right)
      if (user_body.IsActive()) user_body.RotateDegrees(-45.0);
      break;
    case 40:                                      // DOWN ARROW (Breaks)
      if (user_body.IsActive()) user_body.DecSpeed();
      break;

    default:
      return_value = false;
    };

    return return_value;
  }
};

AvidaWeb avida_web;

int emp_main() {
  avida_web.Init();
}

// int main(int argc, char * argv[])
// {
//   Avida avida(emp::ArgsToStrings(argc, argv));
//   avida.Setup();
//   avida.Run();
// }