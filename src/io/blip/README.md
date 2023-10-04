#  BLIP For Crouton

This is an implementation of the [BLIP RPC protocol][BLIP] using Crouton APIs.

## License

Unlike the rest of Crouton, **the source files in this directory are not Apache-licensed**, because
 they're adapted from [code in Couchbase Lite Core][LITECORE] which uses the [Business Software
 License][BSL].

    Use of this software is governed by the Business Source License included
    in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
    in that file, in accordance with the Business Source License, use of this
    software will be governed by the Apache License, Version 2.0, included in
    the file licenses/APL2.txt.

However, on November 1, 2025 this source code will revert to the Apache license.

## Using It

For the above reasons this code is not compiled as part of the regular CMake build. 

To build it, enable the CMake option `CROUTON_BUILD_BLIP`, i.e. add `-DCROUTON_BUILD_BLIP=1` to the `cmake` command line. This will build a separate static library `libBLIP.a`.

[BLIP]: https://github.com/couchbase/couchbase-lite-core/blob/master/Networking/BLIP/docs/BLIP%20Protocol.md
[LITECORE]: https://github.com/couchbase/couchbase-lite-core/tree/master/Networking/BLIP
[BSL]: licenses/APL2.txt
