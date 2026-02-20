/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "core/Avida.hpp"

#include "Modules/LogModule.hpp"

using namespace avida;

int main(int argc, char * argv[])
{
  using avida_t = Avida<AvidaVM, LogModule>;
  avida_t avida(emp::ArgsToStrings(argc, argv));
  avida.Setup();
  avida.GetPlugIn<LogModule>().SetOutFile("avida.log");
  avida.Run(10000);
}