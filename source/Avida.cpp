/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/vector.hpp"
#include "emp/config/command_line.hpp"

#include "Hardware/AvidaVM.hpp"

/// Main Avida-control object.
class Avida {
private:
  emp::Random random;
public:
  Avida(emp::vector<emp::String> args) {
    (void) args;
  }

  void Trace(AvidaVM & vm, size_t cpu_cycles=200) {
    for (size_t i = 0; i < cpu_cycles; ++i) {
      std::cout << "STEP " << i << ":\n" << vm.StatusString() << std::endl;
      vm.ProcessInst();
    }
    std::cout << "STEP " << cpu_cycles << ":\n" << vm.StatusString() << std::endl;
  }

  void Run() {
    constexpr size_t NUM_TRIALS = 500000;

    auto inst_set = AvidaVM::BuildInstSet();
    Genome genome = inst_set.LoadGenome("../config/ancestor.org");
    AvidaVM org(inst_set, genome);
    for (size_t trial = 0; trial < NUM_TRIALS; ++trial) {
      if (trial % 100000 == 0) std::cout << "Trial: " << trial << std::endl; 
      inst_set.BuildGenome(genome, 256, random);
      // org.Reset(genome);
      for (size_t i = 0; i < 200; ++i) {
        org.ProcessInst();
      }
    }
  }
};

int main(int argc, char * argv[])
{
  Avida avida(emp::ArgsToStrings(argc, argv));
  avida.Run();
}