#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module maintains a grid-based population.
 * 
 *  Neighbor layout:
 *    7 0 1
 *    6 X 2
 *    5 4 3
 */

#include <cstddef>   // for size_t
#include <iostream>
#include <span>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class PopGrid : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  size_t width = 100;
  size_t height = 100;
  size_t num_cells = width * height;

  // emp::array<size_t, num_cells> org_grid{};
  emp::vector<size_t> org_grid;

  // === Helper Functions ===
  [[nodiscard]] size_t ToID(size_t x, size_t y) const {
    return (x % width) + (y % height) * width;
  }

  template <concepts::Organism ORG_T>
  void SetPos(ORG_T & org, size_t x, size_t y) {
    org.GetPhenotype().pop_pos = ToID(x,y);
  }

public:
  static constexpr size_t EMPTY_CELL = static_cast<size_t>(-1);

  PopGrid(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "PopGrid", "PopManager", "Manage a grid-based population")
  { }
  ~PopGrid() { }

  void Serialize(emp::SerialPod & pod) {
    pod(org_grid);
  }

  // === Phenotypic Traits ===

  struct Phenotype {
    size_t pop_pos = EMPTY_CELL;
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(pop_pos, "Position in the grid population of this organism.");
  }

  void RegisterSettings() {
    avida.AddSetting("grid.width", width, "Number of columns in population grid");
    avida.AddSetting("grid.height", height, "Number of rows in population grid");
  }

  size_t GetOrgReserveCount() const { return width * height; }

  // === Population View Accessors ===

  [[nodiscard]] size_t GetWidth() const { return width; }
  [[nodiscard]] size_t GetHeight() const { return height; }
  [[nodiscard]] std::span<const size_t> GetCells() const { return org_grid; }

  // === Signal Listeners ===
  void BeforeStart() {
    num_cells = width * height;
    org_grid.resize(num_cells, EMPTY_CELL);
  }


  // When an offspring is ready, need to determine where to place it.
  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & offspring, ORG_T & parent) {
    // Pick future position for offspring.
    const size_t parent_pos = parent.GetPhenotype().pop_pos;
    const size_t parent_x = parent_pos % width;
    const size_t parent_y = parent_pos / width;
    switch (avida.GetRandom().GetUInt(8)) {
      case 0: SetPos(offspring, parent_x,             parent_y - 1 + height); break;
      case 1: SetPos(offspring, parent_x + 1,         parent_y - 1 + height); break;
      case 2: SetPos(offspring, parent_x + 1,         parent_y             ); break;
      case 3: SetPos(offspring, parent_x + 1,         parent_y + 1         ); break;
      case 4: SetPos(offspring, parent_x,             parent_y + 1         ); break;
      case 5: SetPos(offspring, parent_x - 1 + width, parent_y + 1         ); break;
      case 6: SetPos(offspring, parent_x - 1 + width, parent_y             ); break;
      case 7: SetPos(offspring, parent_x - 1 + width, parent_y - 1 + height); break;
    }
  }

  template <concepts::Organism ORG_T>
  void BeforePlacement(ORG_T & org) {
    size_t & org_pos = org.GetPhenotype().pop_pos;

    // If placed organism does not have a position, it is being injected; pick center or random position
    if (org_pos == EMPTY_CELL) {
      const size_t mid_id = ToID(width/2, height/2);
      if (org_grid[mid_id] == EMPTY_CELL) org_pos = mid_id;
      else org_pos = avida.GetRandom().GetUInt(num_cells);
    }

    // See if we must delete an organism to make room for the new one.
    if (org_grid[org_pos] != EMPTY_CELL) {
      avida.DeleteOrg(org_grid[org_pos]);
      org_grid[org_pos] = EMPTY_CELL;
    }
  }

  template <concepts::Organism ORG_T>
  void OnPlacement([[maybe_unused]] ORG_T & org) {
    const size_t pop_pos = org.GetPhenotype().pop_pos;
    emp_assert(pop_pos != EMPTY_CELL, "Orgs must know where to be placed.");
    emp_assert(org_grid[pop_pos] == EMPTY_CELL, "Org must be placed into empty cells");

    org_grid[pop_pos] = org.GetBiotaID();
  }
};
