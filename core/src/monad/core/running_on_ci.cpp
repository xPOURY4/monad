#include <monad/core/running_on_ci.hpp>

#include <cstdlib> // for getenv()
#include <cstring>

MONAD_NAMESPACE_BEGIN
namespace detail
{
    bool running_on_ci_impl() noexcept
    {
        // The environment variable "CI" is pretty much standard for this
        // nowadays
        auto *v = std::getenv("CI");
        if (v == nullptr) {
            return false;
        }
        return 0 == strcmp(v, "true");
    }
}
MONAD_NAMESPACE_END
