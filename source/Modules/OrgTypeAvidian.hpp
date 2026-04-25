/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track the current generation of each organism in the population.
 */

#include <cstddef>   // for size_t
#include <iostream>

#include "../core/Avida.hpp"
#include "../Hardware/AvidaVM.hpp"

template <typename AVIDA_T>
class OrgTypeAvidian : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

  AvidaVM::inst_set_t inst_set;

public:
  OrgTypeAvidian(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("OrgTypeAvidian", "Representation", "Organism based on AvidaVM hardware.")
    , avida(avida) {}
  ~OrgTypeAvidian() {}

  // === Phenotypic Traits ===

  struct Phenotype {
    AvidaVM hardware;  // Hardware run by this organism.
  };

  struct GlobalTypes {
    using hardware_t = AvidaVM;
    using genome_t   = AvidaVM::genome_t;
    using inst_set_t = AvidaVM::inst_set_t;
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(hardware, "Virtual CPU for this organism");
  }

  void RegisterCallbacks() {
    AvidaVM::BuildInstSet(inst_set);  // Build the standard AvidaVM instruction set.
  }

  void AddCallback(const emp::String & name, std::function<void(size_t)> callback) {
    AvidaVM::AddCallback(inst_set, name, callback);
  }

  // === Signal Listeners ===

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) {
    org.GetPhenotype().hardware.SetInstSet(inst_set);
  }

  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & offspring, ORG_T & /*parent*/) {
    offspring.GetPhenotype().hardware.SetInstSet(inst_set);
  }

  // === Handlers ===

  std::expected<typename GlobalTypes::genome_t, emp::String> LoadGenome(const std::filesystem::path & filepath) {
    return inst_set.LoadGenome(filepath);
  }


};
