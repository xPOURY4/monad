#pragma once

#include <monad/vm/compiler/ir/x86/types.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/interpreter/intercode.hpp>

namespace monad::vm
{
    using SharedIntercode = std::shared_ptr<interpreter::Intercode const>;
    using SharedNativecode =
        std::shared_ptr<compiler::native::Nativecode const>;

    class VarcodeCache;

    class Varcode
    {
        friend class VarcodeCache;

        Varcode() {}

    public:
        Varcode(SharedIntercode icode)
            : intercode_{std::move(icode)}
        {
        }

        Varcode(SharedIntercode icode, SharedNativecode ncode)
            : intercode_{std::move(icode)}
            , nativecode_{std::move(ncode)}
        {
        }

        /// Get corresponding intercode.
        /// Can be assumed to always return a non-null result.
        SharedIntercode const &intercode() const
        {
            return intercode_;
        }

        /// Get corresponding nativecode. Returns null if no native code.
        SharedNativecode const &nativecode() const
        {
            return nativecode_;
        }

        std::size_t code_size_estimate() const
        {
            std::size_t x = intercode_->code_size();
            x += nativecode_ ? nativecode_->native_code_size_estimate() : 0;
            return x;
        }

    private:
        SharedIntercode intercode_;
        SharedNativecode nativecode_;
    };
}
