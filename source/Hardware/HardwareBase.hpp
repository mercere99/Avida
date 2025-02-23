#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "../Genome.hpp"

class HardwareBase {
public:
  virtual ~HardwareBase() { }

  virtual void Run(size_t cycles=10) = 0;
  virtual void Reset() = 0;
  virtual void Reset(const Genome & in_genome) = 0;
};