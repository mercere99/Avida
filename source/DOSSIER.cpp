/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "core/Avida.hpp"

#include "Modules/DriverTournament.hpp"
#include "Modules/LogModule.hpp"
#include "Modules/OrgTypeDOSSIER.hpp"
#include "Modules/TrackGeneration.hpp"
#include "Modules/TrackGenotypes.hpp"

// Which DRIVER to use?
template <typename T> using driver_t = DriverTournament<T>;

int main(int argc, char * argv[])
{
  using avida_t = Avida<OrgTypeDOSSIER, driver_t>;

  avida_t avida(emp::ArgsToStrings(argc, argv));
  avida.OK();
  // avida.GetPlugIn<LogModule>().SetOutFile("avida.log");
  avida.Run();
}
