/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "Avida.hpp"

int main(int argc, char * argv[])
{
  Avida avida(emp::ArgsToStrings(argc, argv));
  avida.Setup();
  avida.Run();
}