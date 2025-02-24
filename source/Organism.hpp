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
  emp::Ptr<HardwareBase> hw_ptr;

  uint32_t generation = 0; 
public:
  Organism(HardwareManager & hm) : hw_ptr(hm.Allocate()) { }
};
