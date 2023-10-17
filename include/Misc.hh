//
// Misc.hh
//
// 
//

#pragma once
#include "util/Base.hh"

namespace crouton {

    /// Writes cryptographically-secure random bytes to the destination buffer.
    void Randomize(void* buf, size_t len);

}
