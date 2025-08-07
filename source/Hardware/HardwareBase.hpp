#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/math/Random.hpp"

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

  [[nodiscard]] Organism & GetOrganism() { emp_assert(org_ptr); return *org_ptr; }
  [[nodiscard]] const Organism & GetOrganism() const { emp_assert(org_ptr); return *org_ptr; }
  HardwareBase & SetOrganism(Organism & org) { org_ptr = &org; return *this; }

  [[nodiscard]] HardwareManager & GetManager() { return hw_manager; }
  [[nodiscard]] const HardwareManager & GetManager() const { return hw_manager; }

  virtual void Process(size_t cycles=10) = 0;
  virtual void Reset() = 0;
  virtual void Reset(const Genome & in_genome) = 0;
  [[nodiscard]] virtual Genome DivideGenome(emp::Random & random) = 0;

  virtual bool OK([[maybe_unused]] bool check_org_ok=true) const { return true; }
};