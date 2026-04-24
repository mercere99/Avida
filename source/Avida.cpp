/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "core/Avida.hpp"

#include "Modules/DriverBasic.hpp"
#include "Modules/LogModule.hpp"
#include "Modules/MutationsDivideSub.hpp"
#include "Modules/OrgTypeAvidian.hpp"
#include "Modules/PopGrid.hpp"
#include "Modules/PopWellMixed.hpp"
#include "Modules/TrackGeneration.hpp"
#include "Modules/TrackGenotypes.hpp"

int main(int argc, char * argv[])
{
  // Well Mixed:
  // using avida_t = Avida<OrgTypeAvidian, PopWellMixed, DriverBasic, MutationsDivideSub, TrackGeneration>;

  // Grid:
  using avida_t = Avida<OrgTypeAvidian, PopGrid, DriverBasic, MutationsDivideSub, TrackGeneration, TrackGenotypes>;
  // using avida_t = Avida<OrgTypeAvidian, PopGrid, DriverBasic, MutationsDivideSub, TrackGeneration>;

  avida_t avida(emp::ArgsToStrings(argc, argv));
  avida.OK();
  // avida.GetPlugIn<LogModule>().SetOutFile("avida.log");
  avida.Run();
}
