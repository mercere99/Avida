/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// IN DEVELOPMENT
// Compiles with: g++ -std=c++23 -Wall -Wextra -Wno-unused-function -Wnon-virtual-dtor -Wcast-align -Woverloaded-virtual -pedantic -Isource/ -I../Empirical/include/  -O3 -DNDEBUG -march=native source/Avida-threaded.cpp -o build/Avida -ltbb -L/opt/homebrew/lib -I/opt/homebrew/include

#include "core/Avida.hpp"

#include "Modules/DriverThreaded.hpp"
#include "Modules/LogModule.hpp"
#include "Modules/MutationsDivideSub.hpp"
#include "Modules/OrgTypeAvidian.hpp"
#include "Modules/PopGrid.hpp"
#include "Modules/PopWellMixed.hpp"
#include "Modules/TrackGeneration.hpp"
#include "Modules/TrackGenotypes.hpp"

// Which POPULATION to use?
template <typename T> using pop_t = 
// PopWellMixed<T>;
PopGrid<T>;

// Which DRIVER to use?
template <typename T> using driver_t = DriverThreaded<T>;

int main(int argc, char * argv[])
{
  using avida_t = Avida<OrgTypeAvidian, pop_t, driver_t, MutationsDivideSub, TrackGeneration>;

  avida_t avida(emp::ArgsToStrings(argc, argv));
  avida.OK();
  avida.Run();
}
