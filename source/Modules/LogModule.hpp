#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module allows a user to log signals that have been triggered (used mostly for debugging)
 */

#include <cstddef>   // for size_t
#include <fstream>
#include <iostream>

#include "emp/base/Ptr.hpp"

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class LogModule : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;
  emp::Ptr<std::ostream> os_ptr = &std::cout;
  std::string filename{};  // File to print to; empty means standard out.

  template <typename... Ts>
  void Log(Ts... args) {
    ((*os_ptr << std::forward<Ts>(args)), ...);
    *os_ptr << '\n';
  }
public:
  LogModule(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "LogModule", "Analysis",
        "Record each time a signal is triggered") { }
  ~LogModule() { if (filename.size()) os_ptr.Delete(); }

  void Serialize(emp::SerialPod & /* pod */) {
    // Nothing extra to serialize; everything should be in the SettingsManager
  }

  // === Configuration Helpers ===

  void SetOutFile(std::string name) {
    emp_assert(name.size() > 0);

    // If we already have a file open, close it.
    if (filename.size()) os_ptr.Delete();
    filename = name;
    os_ptr.NewDerived<std::ofstream>(filename);
  }

  // === Signal Listeners ===

  void OnUpdateStart(size_t update) {
    Log("==> Starting update:", update, "; Num orgs = ", avida.GetNumOrgs());
  }

  void OnUpdateEnd(size_t update) {
    Log("Ending update:", update, " <==");
  }

  template <concepts::Organism ORG_T>
  void BeforeRepro(ORG_T & parent) {
    Log("Org at biota_id=", parent.GetBiotaID(), " about to replicate");
  }

  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & offspring, ORG_T & parent) {
    Log("Offspring ready from biota_id=", parent.GetBiotaID(), " : ", offspring.GetGenomeSequence());
  }

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & inject_org) {
    Log("Organism Injection ready : ", inject_org.GetGenomeSequence());
  }

  template <concepts::Organism ORG_T>
  void BeforePlacement(ORG_T & org) {
    Log("Organism (global_id=", org.GetGlobalID(), ") about to be placed into population");
  }

  template <concepts::Organism ORG_T>
  void OnPlacement(ORG_T & org) {
    Log("New org placed at biota_id=", org.GetBiotaID(), "!");
  }

  template <concepts::Organism ORG_T>
  void BeforeDeath(ORG_T & org) {
    Log("Org at biota_id=", org.GetBiotaID(), " about to DIE!");
  }

  void BeforeExit() { Log("Program about to exit..."); }

  void OnHelp() { Log("Help has been requested!"); }
};
