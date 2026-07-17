/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "core/Avida.hpp"

#include "Modules/DriverLexicase.hpp"
#include "Modules/DriverTournament.hpp"
#include "Modules/DriverLexicase.hpp"
#include "Modules/OrgTypeROMEO.hpp"
#include "Modules/TrackOffspringCount.hpp"

// Which DRIVER to use?
template <typename T> using driver_t =
DriverLexicase<T>;
// DriverTournament<T>;

int main(int argc, char * argv[])
{
  using romeo_t = Avida<OrgTypeROMEO, driver_t, TrackOffspringCount>;
  // using romeo_t = Avida<OrgTypeDOSSIER, driver_t>;

  romeo_t romeo(emp::ArgsToStrings(argc, argv));
  romeo.OK();
  romeo.Run();
}
