/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This is a starter plug-in module, with all of the signals listed, but none set to do anything.
 */

#include <cstddef>   // for size_t
#include <fstream>
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class ModuleTemplate : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

public:
  ModuleTemplate(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("ModuleName", "ModuleType", "Module description text"), avida(avida) { }
  ~ModuleTemplate() { }

  // === Phenotypic Traits ===

  // struct Phenotype {
  //   AVIDA_TRAIT(size_t, generation, "Number of offspring in chain since inject");
  // };

  // === Signal Listeners ===

  OnUpdateStart([[maybe_unused]] size_t new_update) { }

  OnUpdateEnd([[maybe_unused]] size_t old_update) { }

  template <concepts::Organism ORG_T>
  BeforeRepro([[maybe_unused]] ORG_T & parent) { }

  template <concepts::Organism ORG_T>
  OnOffspringReady([[maybe_unused]] ORG_T & offspring, [[maybe_unused]] ORG_T & parent) { }

  template <concepts::Organism ORG_T>
  OnInjectReady([[maybe_unused]] ORG_T & inject_org) { }

  template <concepts::Organism ORG_T>
  BeforePlacement([[maybe_unused]] ORG_T & org) { }

  template <concepts::Organism ORG_T>
  OnPlacement([[maybe_unused]] ORG_T & org) { }

  template <concepts::Organism ORG_T>
  BeforeMutate([[maybe_unused]] ORG_T & org) { }

  template <concepts::Organism ORG_T>
  OnMutate([[maybe_unused]] ORG_T & org) { }

  template <concepts::Organism ORG_T>
  BeforeDeath([[maybe_unused]] ORG_T & org) { }

  BeforeExit() { }
  
  OnHelp() { }
};
