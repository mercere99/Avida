#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module allows a user to log signals that have been triggered (used mostly for debugging)
 */

#include <cstddef>   // for size_t
#include <fstream>
#include <iostream>

#include "emp/base/Ptr.hpp"

#include "../core/Avida.hpp"
#include "../core/concepts.hpp"

namespace avida {

  template <typename AVIDA_T>
  class LogModule {
  private:
    AVIDA_T & avida;
    emp::Ptr<std::ostream> os_ptr = &std::cout;
    std::string filename{};  // File to print to; empty mean standard out.

    template <typename... Ts>
    bool Log(Ts... args) {
      ((*os_ptr << std::forward<Ts>(args)), ...);
      *os_ptr << '\n';
      return true;
    }
  public:
    LogModule(AVIDA_T & avida) : avida(avida) { }
    ~LogModule() { if (filename.size()) os_ptr.Delete(); }

    void SetOutFile(std::string name) {
      emp_assert(name.size() > 0);

      // If we already have a file open, close it.
      if (filename.size()) os_ptr.Delete();
      filename = name;
      os_ptr.NewDerived<std::ofstream>(filename);
    }

    // === Signal Listeners ===

    bool OnUpdateStart(size_t new_update) {
      return Log("==> Starting update:", new_update, "; Num orgs = ", avida.GetNumOrgs());
    }

    bool OnUpdateEnd(size_t old_update) {
      return Log("Ending update:", old_update, " <==");
    }

    template <concepts::Organism ORG_T>
    bool BeforeRepro(ORG_T & parent) {
      return Log("Org at ", parent.GetPosition(), " about to replicate");
    }

    template <concepts::Organism ORG_T>
    bool OnOffspringReady(ORG_T & offspring, ORG_T & parent) {
      return Log("Offspring ready from ", parent.GetPosition(), " : ", offspring.GetGenomeSequence());
    }

    template <concepts::Organism ORG_T>
    bool OnInjectReady(ORG_T & inject_org) {
      return Log("Organism Injection ready : ", inject_org.GetGenomeSequence());
    }

    template <concepts::Organism ORG_T>
    bool BeforePlacement(ORG_T & org) {
      return Log("Organism (", org.GetID(), ") about to be placed into population)");
    }

    template <concepts::Organism ORG_T>
    bool OnPlacement(ORG_T & org) {
      return Log("New org placed as position ", org.GetPosition(), "!");
    }

    template <concepts::Organism ORG_T>
    bool BeforeMutate(ORG_T & org) {
      return Log("Org at position ", org.GetPosition(), " about to mutate!");
    }

    template <concepts::Organism ORG_T>
    bool OnMutate(ORG_T & org) {
      return Log("Org at position ", org.GetPosition(), " has been mutated!");
    }

    template <concepts::Organism ORG_T>
    bool BeforeDeath(ORG_T & org) {
      return Log("Org at position ", org.GetPosition(), " about to DIE!");
    }

    bool BeforeExit() { return Log("Program about to exit..."); }

    bool OnHelp() { return Log("Help has been requested!"); }
  };


} // namespace avida
