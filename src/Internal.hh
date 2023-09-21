//
// Internal.hh
//
// 
//

#pragma once
#include "Base.hh"
#include "Error.hh"
#include <span>

namespace crouton {

    /** Utility for implementing name lookup, as in `ErrorDomainInfo<T>::description`.*/
    struct NameEntry {
        errorcode_t code;
        const char* name;

        /// Given a code, finds the first matching entry in the list and returns its name, else "".
        static string lookup(int code, std::span<const NameEntry>);
    };



    class NotReentrant {
    public:
        explicit NotReentrant(bool& scope)
        :_scope(scope)
        {
            if (_scope)
                Error::raise(CroutonError::LogicError, "Illegal reentrant call");
            _scope = true;
        }

        ~NotReentrant() {_scope = false;}

    private:
        bool& _scope;
    };

}
