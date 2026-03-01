#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  Common base class for all organisms.
 */

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t

// Common base class for all organism types.
class OrganismBase {
public:
  static constexpr size_t UNKNOWN_ID = static_cast<size_t>(-1);
protected:

  size_t biota_id = UNKNOWN_ID;   // Where is this Organism located?
  size_t global_id = UNKNOWN_ID;  // Unique organism ID.

  OrganismBase() = default;
  OrganismBase(OrganismBase && in) : biota_id(in.biota_id), global_id(in.global_id) {
    in.biota_id = UNKNOWN_ID;
    in.global_id = UNKNOWN_ID;
  }

public:
  [[nodiscard]] size_t GetBiotaID() const { return biota_id; }
  auto & SetBiotaID(this auto & self, size_t in_pos) { self.biota_id = in_pos; return self; }

  [[nodiscard]] size_t GetGlobalID() const { return global_id; }
  auto & SetGlobalID(this auto & self, size_t in_id) { self.global_id = in_id; return self; }

};

