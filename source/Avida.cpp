/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "core/Avida.hpp"

using namespace avida;

int main(int argc, char * argv[])
{
  Avida<AvidaVM> avida(emp::ArgsToStrings(argc, argv));
  avida.Setup();
  avida.Run(10000);
}