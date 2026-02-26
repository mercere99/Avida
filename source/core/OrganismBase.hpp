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
protected:
  static constexpr size_t UNKNOWN_ID = static_cast<size_t>(-1);

  size_t position = UNKNOWN_ID;     // Where is this Organism located?
  size_t id = UNKNOWN_ID;           // Unique organism ID.
  uint32_t generation = 0;          // Number of ancestral steps back to injected organism.

  OrganismBase() = default;
  OrganismBase(OrganismBase && in) : position(in.position), id(in.id), generation(in.generation) {
    in.position = UNKNOWN_ID;
    in.id = UNKNOWN_ID;
  }
  OrganismBase(const OrganismBase & in) : generation(in.generation + 1) { }

};

