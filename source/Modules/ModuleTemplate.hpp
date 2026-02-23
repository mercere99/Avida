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
#include "../core/concepts.hpp"

namespace avida {

  template <typename AVIDA_T>
  class ModuleTemplate {
  private:
    AVIDA_T & avida;

  public:
    ModuleTemplate(AVIDA_T & avida) : avida(avida) { }
    ~ModuleTemplate() { }

    constexpr static std::string GetName() { static_assert(false, "Must output a module name."); }
    constexpr static std::string GetName() { static_assert(false, "Must output a module type."); }

    // === Signal Listeners ===

    bool OnUpdateStart([[maybe_unused]] size_t new_update) {
      return true;
    }

    bool OnUpdateEnd([[maybe_unused]] size_t old_update) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforeRepro([[maybe_unused]] ORG_T & parent) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnOffspringReady([[maybe_unused]] ORG_T & offspring, [[maybe_unused]] ORG_T & parent) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnInjectReady([[maybe_unused]] ORG_T & inject_org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforePlacement([[maybe_unused]] ORG_T & org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnPlacement([[maybe_unused]] ORG_T & org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforeMutate([[maybe_unused]] ORG_T & org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnMutate([[maybe_unused]] ORG_T & org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforeDeath([[maybe_unused]] ORG_T & org) {
      return true;
    }

    bool BeforeExit() {      
      return true;
    }
    
    bool OnHelp() {
      return true;
      
    }
  };


} // namespace avida
