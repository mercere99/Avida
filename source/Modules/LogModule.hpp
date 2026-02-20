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

    bool BeforeUpdate(size_t old_update) { return Log("Ending update:", old_update); }
    bool OnUpdate(size_t new_update) { return Log("Starting update:", new_update); }
    bool BeforeRepro(size_t parent_pos) { return Log("Org ", parent_pos, "about to replicate"); }

    template <concepts::Organism ORG_T>
    bool OffspringReady(ORG_T & offspring, size_t parent_pos) {
      return Log("Offspring ready from ", parent_pos, " : ", offspring.GetGenomeSequence());
    }

    template <concepts::Organism ORG_T>
    bool InjectReady(ORG_T & inject_org) {
      return Log("Organism Injection ready : ", inject_org.GetGenomeSequence());
    }

    template <concepts::Organism ORG_T>
    bool BeforePlacement(ORG_T & org, size_t target_pos, size_t parent_pos) {
      return Log("Organism (", org.GetID(), ") about to be placed at position ", target_pos,
                 " (with parent at position ", parent_pos, ")");
    }

    bool OnPlacement(size_t pos) { return Log("Welcome new or at position ", pos, "!"); }
    bool BeforeMutate(size_t pos) { return Log("Org at position ", pos, " about to mutate!"); }
    bool OnMutate(size_t pos) { return Log("Org at position ", pos, " has been mutated!"); }
    bool BeforeDeath(size_t pos) { return Log("Org at position ", pos, " about to DIE!"); }
    bool BeforeExit() { return Log("Program about to exit..."); }
    bool OnHelp() { return Log("Help has been requested!"); }
  };


} // namespace avida
