#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "../Genome.hpp"

// Forward declarations
class HardwareManager;
class Organism;

class HardwareBase {
protected:
  HardwareManager & hw_manager;
  emp::Ptr<Organism> org_ptr = nullptr;
public:
  HardwareBase(HardwareManager & hw_manager) : hw_manager(hw_manager) { }
  virtual ~HardwareBase() { }

  Organism & GetOrganism() { emp_assert(org_ptr); return *org_ptr; }
  HardwareBase & SetOrganism(Organism & org) { org_ptr = &org; return *this; }

  virtual void Run(size_t cycles=10) = 0;
  virtual void Reset() = 0;
  virtual void Reset(const Genome & in_genome) = 0;
};