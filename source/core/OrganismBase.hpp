#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  Common base class for all organisms.
 */

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t

// Common base class for all organism types.
class OrganismBase {
public:
  /// Reserved Biota IDs describe objects that are not in an ordinary population slot.
  static constexpr size_t NO_BIOTA_ID = static_cast<size_t>(-1);
  static constexpr size_t ANALYSIS_BIOTA_ID = NO_BIOTA_ID - 1;
  static constexpr size_t NO_GLOBAL_ID = static_cast<size_t>(-1);

protected:
  size_t biota_id = NO_BIOTA_ID;  // Where is this Organism stored?
  size_t global_id = NO_GLOBAL_ID;  // Unique organism ID.
  bool is_mutant = false;         // Is this organism different from its parent?

  OrganismBase() = default;
  OrganismBase(OrganismBase && in) : biota_id(in.biota_id), global_id(in.global_id) {
    in.biota_id = NO_BIOTA_ID;
    in.global_id = NO_GLOBAL_ID;
  }

public:
  [[nodiscard]] size_t GetBiotaID() const { return biota_id; }
  [[nodiscard]] bool IsAnalysis() const { return biota_id == ANALYSIS_BIOTA_ID; }
  [[nodiscard]] bool HasLiveBiotaID() const { return biota_id < ANALYSIS_BIOTA_ID; }

  [[nodiscard]] size_t GetGlobalID() const { return global_id; }
  auto & SetGlobalID(this auto & self, size_t in_id) { self.global_id = in_id; return self; }

  [[nodiscard]] bool IsMutated() const { return is_mutant; }
  void SetMutated(bool in=true) { is_mutant=in; }

};
