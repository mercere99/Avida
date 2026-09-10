#pragma once

#include <istream>
#include "Modules/OrgTypeAvidian.hpp"
#include "SelectAdapter.hpp"

namespace avida_web {

template <typename MODULE_T>
struct OrganismAdapter {};

// Genome I/O and checkpoint identity belong to the organism type. Hardware-specific inspection
// lives in OrganismAnalysis.hpp; a numeric-genome interface will need its own inspection view.
template <typename AVIDA_T>
class OrganismAdapter<OrgTypeAvidian<AVIDA_T>> {
  AVIDA_T & avida;

public:
  using supported_t = void;
  static constexpr const char * CPU_PROFILE = "AvidaVM-v1";
  static constexpr const char * INSTRUCTION_PROFILE = "AvidaVM-standard-v2-no-xor";

  explicit OrganismAdapter(AVIDA_T & avida) : avida(avida) { }

  auto LoadGenome(std::istream & input) {
    return avida.template GetPlugIn<OrgTypeAvidian>().LoadGenome(input);
  }
  auto LoadGenome(const std::filesystem::path & filename) {
    return avida.template GetPlugIn<OrgTypeAvidian>().LoadGenome(filename);
  }
  static emp::String GenomeText(const typename AVIDA_T::organism_t & organism) {
    emp::String text;
    const auto & instructions = organism.Hardware().GetInstSet();
    for (const auto id : organism.GetGenome()) {
      text += instructions.GetName(id);
      text += '\n';
    }
    return text;
  }
};

}
