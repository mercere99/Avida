/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/vector.hpp"
#include "emp/config/command_line.hpp"


/// Main Avida-control object.
class Avida {
private:
public:
  Avida(emp::vector<emp::String> args) {
    
  }
};

int main(int argc, char * argv[])
{
  Avida avida(emp::ArgsToStrings(argc, argv));
}