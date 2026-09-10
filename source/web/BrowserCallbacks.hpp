#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

void AvidaWebApp::InitializeDragCallbacks() {
  if (!drop_organism_callback_id) {
    drop_organism_callback_id = emp::JSWrap(std::function<void(size_t, size_t)>{
      [this](size_t freezer_id, size_t cell_id) {
        DropFrozenOrganismOnGrid(freezer_id, cell_id);
      }
    });
  }
  if (!freeze_grid_callback_id) {
    freeze_grid_callback_id = emp::JSWrap(std::function<void(size_t)>{
      [this](size_t cell_id) { FreezeOrUnstageGridCell(cell_id); }
    });
  }
  if (!rename_freezer_callback_id) {
    rename_freezer_callback_id = emp::JSWrap(std::function<void(size_t, size_t, std::string)>{
      [this](size_t item_type, size_t item_id, std::string name) {
        if (item_type == 0) {
          RenameFrozenItem(freezer.organisms, item_id, name);
        } else if (item_type == 1) {
          RenameFrozenItem(freezer.configurations, item_id, name);
        }
      }
    });
  }
  if (!checkpoint_file_callback_id) {
    checkpoint_file_callback_id = emp::JSWrap(std::function<void(size_t, size_t)>{
      [this](size_t buffer, size_t size) {
        const char * bytes = reinterpret_cast<const char *>(buffer);
        ImportCheckpointFile(std::string{bytes, size});
      }
    });
    InstallCheckpointFileBridge(checkpoint_file_callback_id);
  }
  if (!population_step_callback_id) {
    population_step_callback_id = emp::JSWrap(std::function<void()>{
      [this](){ StepPopulation(); }
    });
  }
  if (!population_fast_forward_callback_id) {
    population_fast_forward_callback_id = emp::JSWrap(std::function<void()>{
      [this](){ ToggleRunMode(RunMode::FAST_FORWARD); }
    });
  }
  if (!organism_space_step_callback_id) {
    organism_space_step_callback_id = emp::JSWrap(std::function<void()>{
      [this](){ StepOrganismInstruction(); }
    });
  }
  if (!organism_scrub_callback_id) {
    organism_scrub_callback_id = emp::JSWrap(std::function<void(std::string)>{
      [this](std::string value){ JumpToOrganismExecutionPosition(value); }
    });
  }
  if (!organism_head_callback_id) {
    organism_head_callback_id = emp::JSWrap(std::function<void(size_t)>{
      [this](size_t head_id){ SetTrackedOrganismHead(head_id); }
    });
  }
}
