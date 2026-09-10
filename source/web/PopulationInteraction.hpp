#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

bool AvidaWebApp::InjectGenomeAtCell(const emp::String & genome_text, size_t cell_id) {
  if (cell_id >= PopulationWidth() * PopulationHeight()) return false;
  std::istringstream genome_input{genome_text.str()};
  auto genome = Organisms().LoadGenome(genome_input);
  if (!genome) return false;

  return Population().InjectGenome(std::move(*genome), cell_id);
}

void AvidaWebApp::ApplyPlacedOrganisms() {
  for (const auto & placement : placed_organisms) {
    (void) InjectGenomeAtCell(placement.genome, placement.cell_id);
  }
}

bool AvidaWebApp::HasGridCellOrganism(size_t cell_id) {
  if (!run_started) {
    return std::find_if(
      placed_organisms.begin(), placed_organisms.end(),
      [cell_id](const auto & item){ return item.cell_id == cell_id; }
    ) != placed_organisms.end();
  }

  const auto cells = Population().GetCells();
  if (cell_id >= cells.size()) return false;
  const size_t organism_id = cells[cell_id];
  return organism_id != avida_web::EMPTY_CELL && Avida().IsOccupied(organism_id);
}

void AvidaWebApp::OpenGridCellMenu(size_t cell_id, int client_x, int client_y) {
  if (SimulationWorkerBusy()) {
    SetRunMode(RunMode::PAUSED);
    HideGridCellMenu();
    return;
  }
  if (!HasGridCellOrganism(cell_id)) {
    HideGridCellMenu();
    return;
  }
  if (run_started && run_mode != RunMode::PAUSED) SetRunMode(RunMode::PAUSED);
  grid_context_cell_id = cell_id;
  ShowGridCellMenu(client_x, client_y);
}

void AvidaWebApp::SaveGridCellOrganism(size_t cell_id) {
  if (SimulationWorkerBusy()) return;
  if (!run_started) {
    const auto iterator = FindPlacedOrganism(cell_id);
    if (iterator == placed_organisms.end()) return;
    freezer.organisms.push_back({
      .id = freezer.next_id++,
      .name = emp::MakeString(iterator->name, " copy"),
      .genome = iterator->genome,
      .instruction_count = iterator->instruction_count
    });
    (void) PersistFreezer();
    freezer_message = emp::MakeString("Saved staged ", iterator->name, ".");
    RequestInterfaceRebuild();
    return;
  }

  const auto cells = Population().GetCells();
  if (cell_id >= cells.size()) return;
  const size_t organism_id = cells[cell_id];
  if (organism_id == avida_web::EMPTY_CELL || !Avida().IsOccupied(organism_id)) return;
  const auto & organism = Avida().GetOrg(organism_id);
  freezer.organisms.push_back({
    .id = freezer.next_id++,
    .name = emp::MakeString(
      "Organism #", organism.GetGlobalID(), " at update ", Avida().GetUpdate()
    ),
    .genome = GenomeFileText(organism),
    .instruction_count = organism.GetGenome().size()
  });
  (void) PersistFreezer();
  freezer_message = emp::MakeString("Saved organism #", organism.GetGlobalID(), ".");
  RequestInterfaceRebuild();
}

void AvidaWebApp::RemoveGridCellOrganism(size_t cell_id) {
  if (SimulationWorkerBusy()) {
    SetRunMode(RunMode::PAUSED);
    return;
  }
  if (!run_started) {
    const auto iterator = FindPlacedOrganism(cell_id);
    if (iterator == placed_organisms.end()) return;
    const emp::String name = iterator->name;
    placed_organisms.erase(iterator);
    if (active_cell_id == cell_id) active_cell_id = avida_web::EMPTY_CELL;
    freezer_message = emp::MakeString("Removed staged ", name, " from the grid.");
    RequestInterfaceRebuild();
    return;
  }

  if (run_mode != RunMode::PAUSED) SetRunMode(RunMode::PAUSED);
  const auto cells = Population().GetCells();
  if (cell_id >= cells.size()) return;
  const size_t organism_id = cells[cell_id];
  if (organism_id == avida_web::EMPTY_CELL || !Avida().IsOccupied(organism_id)) return;
  const size_t global_id = Avida().GetOrg(organism_id).GetGlobalID();
  if (active_cell_id == cell_id) {
    active_organism = {};
    active_cell_id = avida_web::EMPTY_CELL;
  }
  if (!Population().DeleteOrganismAt(cell_id)) return;
  freezer_message = emp::MakeString("Removed organism #", global_id, " from the grid.");
  DrawPopulation();
  RefreshReadouts();
  UpdateControls();
}

void AvidaWebApp::DropFrozenOrganismOnGrid(size_t freezer_id, size_t cell_id) {
  if (SimulationWorkerBusy() || run_mode != RunMode::PAUSED) return;
  if (cell_id >= PopulationWidth() * PopulationHeight()) return;
  const auto iterator = std::find_if(
    freezer.organisms.begin(), freezer.organisms.end(),
    [freezer_id](const auto & item){ return item.id == freezer_id; }
  );
  if (iterator == freezer.organisms.end()) return;

  if (run_started) {
    if (!InjectGenomeAtCell(iterator->genome, cell_id)) return;
    freezer_message = emp::MakeString(
      "Injected ", iterator->name, " ", Population().DescribeInjection(cell_id),
      " as a new organism."
    );
    DrawPopulation();
    RefreshReadouts();
    return;
  }

  PlacedOrganism placement{
    .cell_id = cell_id,
    .name = iterator->name,
    .genome = iterator->genome,
    .instruction_count = iterator->instruction_count
  };
  const auto existing = std::find_if(
    placed_organisms.begin(), placed_organisms.end(),
    [cell_id](const auto & item){ return item.cell_id == cell_id; }
  );
  if (existing == placed_organisms.end()) placed_organisms.push_back(std::move(placement));
  else *existing = std::move(placement);
  active_organism = {};
  active_cell_id = cell_id;
  freezer_message = emp::MakeString(
    "Staged ", iterator->name, " in cell ", cell_id, " for the next run."
  );
  RequestInterfaceRebuild();
}

void AvidaWebApp::FreezeOrUnstageGridCell(size_t cell_id) {
  if (!run_started) {
    RemoveGridCellOrganism(cell_id);
    return;
  }
  SaveGridCellOrganism(cell_id);
}

const avida_t::organism_t * AvidaWebApp::GetActiveOrganism() const {
  return active_organism.TryGet();
}

void AvidaWebApp::UpdateActiveCellHighlight() {
  const bool has_staged_selection = !run_started
    && active_cell_id != avida_web::EMPTY_CELL
    && FindPlacedOrganism(active_cell_id) != placed_organisms.end();
  if (!GetActiveOrganism() && !has_staged_selection) {
    active_organism = {};
    active_cell_id = avida_web::EMPTY_CELL;
  }
  const int display_cell = active_cell_id == avida_web::EMPTY_CELL
    ? -1
    : static_cast<int>(active_cell_id);
  PositionActiveCellHighlight(
    display_cell, static_cast<int>(PopulationWidth()), static_cast<int>(PopulationHeight())
  );
}

void AvidaWebApp::SelectPopulationCell(size_t cell_id) {
  if (SimulationWorkerBusy()) return;
  if (!run_started) {
    active_organism = {};
    active_cell_id = FindPlacedOrganism(cell_id) == placed_organisms.end()
      ? avida_web::EMPTY_CELL
      : cell_id;
    UpdateActiveCellHighlight();
    UpdateControls();
    return;
  }

  const auto cells = Population().GetCells();
  if (cell_id >= cells.size()) return;

  const size_t org_id = cells[cell_id];
  if (org_id == avida_web::EMPTY_CELL || !Avida().IsOccupied(org_id)) {
    active_organism = {};
    active_cell_id = avida_web::EMPTY_CELL;
  } else {
    active_organism = Avida().GetOrgRef(org_id);
    active_cell_id = cell_id;
  }
  UpdateActiveCellHighlight();
  UpdateControls();
  if (active_side_panel == SidePanel::ORGANISM) org_stats_content.Redraw();
}
