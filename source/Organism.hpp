#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/Ptr.hpp"

#include "Hardware/HardwareBase.hpp"
#include "Hardware/HardwareManager.hpp"

/// All organisms in Avida are set up as an Organism class.
class Organism {
private:
  using id_t = uint64_t;
  using gen_t = uint32_t;
  static constexpr id_t UNKNOWN_ID = static_cast<id_t>(-1);

  HardwareManager & hw_man;
  emp::Ptr<HardwareBase> hw_ptr;

  id_t id = UNKNOWN_ID;     // Unique organism ID.
  gen_t generation = 0; 
public:
  Organism(HardwareManager & hw_man, Genome genome)
    : hw_man(hw_man), hw_ptr(hw_man.Allocate(*this)) { hw_ptr->Reset(genome); }

  id_t ID() const { return id; }
  Organism & ID(id_t in_id) { id = in_id; return *this; }

  gen_t Generation() const { return generation; }
  Organism & Generation(gen_t in_gen) { generation = in_gen; return *this; }
};
