//
// Internal.hh
//
// 
//

#pragma once
#include "Base.hh"

namespace crouton {

    class NotReentrant {
    public:
        explicit NotReentrant(bool& scope)
        :_scope(scope)
        {
            if (_scope)
                throw std::logic_error("Illegal reentrant call");
            _scope = true;
        }

        ~NotReentrant() {_scope = false;}

    private:
        bool& _scope;
    };

}
