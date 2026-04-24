#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <cstddef>  // for size_t
#include <cstdint>  // for uint64_t and uint32_t
#include <iostream>

#include "emp/base/Ptr.hpp"
#include "emp/tools/String.hpp"

#include "OrganismBase.hpp"

template <typename GENOME_T, typename PHENOTYPE_T>
class Organism : public OrganismBase {
private:
  GENOME_T genome;        // Original genome for this organism.
  PHENOTYPE_T phenotype;  // Current phenotype for this organism.

public:
  using this_t = Organism<GENOME_T, PHENOTYPE_T>;
  using genome_t = GENOME_T;
  using phenotype_t = PHENOTYPE_T;

  Organism(const Organism &) = delete;   // No direct copying of organism is allowed.
  Organism(Organism && in) = default;

  // Build organism from a genome.
  Organism(genome_t && in_genome) : genome(std::move(in_genome)) {
    if constexpr (HasHardware()) phenotype.hardware.Reset(genome);
  }

  ~Organism() = default;

  Organism & operator=(const Organism&) = delete;  // No direct copying of organism is allowed.

  /// Move assignment
  Organism & operator=(Organism && in) = default;

  void ResetHardware() { if constexpr (HasHardware()) phenotype.hardware.Reset(genome); }

  [[nodiscard]] genome_t & GetGenome() { return genome; }
  [[nodiscard]] const genome_t & GetGenome() const { return genome; }
  [[nodiscard]] emp::String GetGenomeSequence() const {
    // If we have hardware to translate the genome, use it; otherwise return raw genome.
    if constexpr (HasHardware()) {
      return phenotype.hardware.GetInstSet().ToSequence(genome);
    } else {
      return genome.AsString();
    }
  }

  [[nodiscard]] phenotype_t & GetPhenotype() { return phenotype; }
  [[nodiscard]] const phenotype_t & GetPhenotype() const { return phenotype; }

  [[nodiscard]] static constexpr bool HasHardware() {
    return requires { std::declval<phenotype_t>().hardware; };
  }
  [[nodiscard]] auto & GetHardware() {
    if constexpr (HasHardware()) return phenotype.hardware;
    else {
      emp::notify::Error("Trying to access `hardware` on Organisms that does not have it.");
      return "Failure";
    }
  }

  Organism & SetBiotaID(size_t id) {
    biota_id = id;
    if constexpr (HasHardware()) phenotype.hardware.SetBiotaID(id);
    return *this;
  }

  void SetGenome(genome_t && in_genome) {
    genome = std::move(in_genome);
    is_mutant = false;
  }

  Organism & Process(size_t cycles) {
    emp_assert(OK());
    if constexpr (HasHardware()) {
      phenotype.hardware.Process(cycles);
    } else {
      emp_assert(false, "Cannot Process organism without hardware.");
    }
    return *this;
  }

  [[nodiscard]] genome_t DivideGenome() {
    emp_assert(OK());
    if constexpr (HasHardware()) {
      return phenotype.hardware.DivideGenome();
    } else {
      return genome; // Return a COPY of the current genome.
    }
  }

  /// Called when organism is about to die.
  Organism & SignalDeath() {
    return *this;
  }

  void Print(std::ostream & os = std::cout) {
    os << "Genome:" << GetGenomeSequence()
      << " biota_id:" << biota_id
      << " global_id:" << global_id;
  }

  /// Check to make sure there aren't any problems with this organism object.
  bool OK() const {
    if constexpr (HasHardware()) {
      if (phenotype.hardware.GetBiotaID() != biota_id) {
        emp::notify::Error("Hardware biota_id = ", phenotype.hardware.GetBiotaID(),
                          " but organism biota_id = ", biota_id, ".");
        return false;
      }

      if (!phenotype.hardware.OK()) {
        emp::notify::Error("Organism: Failed hardware OK() check.");
        return false;
      }

      return true;
    }
  }
};
