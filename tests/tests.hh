//
// tests.hh
//
// 
//

#include "Crouton.hh"
#include <iostream>

#include "catch_amalgamated.hpp"

using namespace std;
using namespace crouton;


// Runs a coroutine that returns `Future<void>`, returning once it's completed.
void RunCoroutine(Future<void> (*test)());


