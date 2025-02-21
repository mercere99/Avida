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

  void Run() {
    auto inst_set = AvidaVM::BuildInstSet();
    Genome genome;
    AvidaVM org(inst_set, genome);
    for (size_t trial = 0; trial < 1000000; ++trial) {
      if (trial % 100000 == 0) std::cout << "Trial: " << trial << std::endl; 
      inst_set.BuildGenome(genome, 512, random);
      // genome = inst_set.LoadGenome("../config/ancestor.org");
      org.Reset(genome);
      for (size_t i = 0; i < 100; ++i) {
        // std::cout << org.StatusString() << std::endl;
        org.ProcessInst();
      }
      // std::cout << org.StatusString() << std::endl;
    }
  }
};

int main(int argc, char * argv[])
{
  Avida avida(emp::ArgsToStrings(argc, argv));
  avida.Run();
}