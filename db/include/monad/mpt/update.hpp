#include <monad/mpt/config.hpp>

#include <monad/core/byte_string.hpp>

#include <optional>

MONAD_MPT_NAMESPACE_BEGIN

struct Data
{
    byte_string_view val;
    byte_string_view aux;
};

static_assert(sizeof(Data) == 32);
static_assert(alignof(Data) == 8);

static_assert(sizeof(std::optional<Data>) == 40);
static_assert(alignof(std::optional<Data>) == 8);

struct Update
{
    unsigned char *key;
    std::optional<Data> opt;
};

static_assert(sizeof(Update) == 48);
static_assert(alignof(Update) == 8);

[[gnu::always_inline]] constexpr bool is_deletion(Update const &u)
{
    return !u.opt.has_value();
}

MONAD_MPT_NAMESPACE_END
