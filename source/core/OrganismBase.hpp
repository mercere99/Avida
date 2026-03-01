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

  size_t biota_pos = UNKNOWN_ID;  // Where is this Organism located?
  size_t id = UNKNOWN_ID;         // Unique organism ID.

  OrganismBase() = default;
  OrganismBase(OrganismBase && in) : biota_pos(in.biota_pos), id(in.id) {
    in.biota_pos = UNKNOWN_ID;
    in.id = UNKNOWN_ID;
  }

public:
  [[nodiscard]] size_t GetPosition() const { return biota_pos; }
  auto & SetPosition(this auto & self, size_t in_pos) { self.biota_pos = in_pos; return self; }

  [[nodiscard]] size_t GetID() const { return id; }
  auto & SetID(this auto & self, size_t in_id) { self.id = in_id; return self; }

};

