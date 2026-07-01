/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "core/Avida.hpp"

#include "Modules/DriverBasic.hpp"
#include "Modules/DriverBuffered.hpp"
#include "Modules/DriverConstant.hpp"
#include "Modules/EnvironmentLogic.hpp"
#include "Modules/OutputManager.hpp"
#include "Modules/LogModule.hpp"
#include "Modules/MutationsDivideSub.hpp"
#include "Modules/OrgTypeAvidian.hpp"
#include "Modules/PopGrid.hpp"
#include "Modules/PopWellMixed.hpp"
#include "Modules/ReactionsManager.hpp"
#include "Modules/TrackGeneration.hpp"
#include "Modules/TrackGenotypes.hpp"
#include "Modules/TrackMetabolism.hpp"

// Which POPULATION to use?
template <typename T> using pop_t = 
// PopWellMixed<T>;
PopGrid<T>;

// Which DRIVER to use?
template <typename T> using driver_t = 
DriverBasic<T>;
// DriverBuffered<T>;
// DriverConstant<T>;

int main(int argc, char * argv[])
{
  using avida_t = Avida<
    OrgTypeAvidian,
    pop_t,
    driver_t,
    MutationsDivideSub,
    TrackGeneration,
    TrackGenotypes,
    EnvironmentLogic,
    ReactionsManager,
    OutputManager,
    TrackMetabolism
  >;


  avida_t avida(emp::ArgsToStrings(argc, argv));
  avida.OK();
  // avida.GetPlugIn<LogModule>().SetOutFile("avida.log");
  avida.Run();
}
