#pragma once

#include <monad/async/concepts.hpp>

MONAD_ASYNC_NAMESPACE_BEGIN

// helper custom status code for Sender `completed()`
enum class sender_errc : uint8_t
{
    unknown = 0,
    operation_must_be_reinitiated, //!< Don't invoke the receiver, instead
                                   //!< reinitiate the operation
    initiation_immediately_completed //!< Returned during initiation to say the
                                     //!< operation was able to complete
                                     //!< immediately
};

class _sender_errc_code_domain;
using sender_errc_code = BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code<
    _sender_errc_code_domain>;
class _sender_errc_code_domain
    : public BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code_domain
{
    friend sender_errc_code;
    using _base = BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code_domain;

public:
    struct value_type
    {
        uintptr_t code : 8; // sender_errc
        uintptr_t value : (8 * sizeof(uintptr_t) - 8);

        constexpr value_type() noexcept
            : code(0)
            , value(0)
        {
        }
        constexpr value_type(sender_errc _code) noexcept
            : code(uint8_t(_code))
            , value(0)
        {
        }
        constexpr value_type(sender_errc _code, uintptr_t _value) noexcept
            : code(uint8_t(_code))
            , value(_value)
        {
            assert((_value >> 56) == 0);
        }
    };
    using _base::string_ref;

    constexpr explicit _sender_errc_code_domain(
        typename _base::unique_id_type id = 0xa88a5a64a7d218d8) noexcept
        : _base(id)
    {
    }
    _sender_errc_code_domain(const _sender_errc_code_domain &) = default;
    _sender_errc_code_domain(_sender_errc_code_domain &&) = default;
    _sender_errc_code_domain &
    operator=(const _sender_errc_code_domain &) = default;
    _sender_errc_code_domain &operator=(_sender_errc_code_domain &&) = default;
    ~_sender_errc_code_domain() = default;

    static inline constexpr const _sender_errc_code_domain &get();

    virtual string_ref name() const noexcept override
    {
        return string_ref("sender_errc domain");
    }

#if BOOST_OUTCOME_VERSION_MAJOR > 2 ||                                         \
    (BOOST_OUTCOME_VERSION_MAJOR == 2 && BOOST_OUTCOME_VERSION_MINOR > 2) ||   \
    (BOOST_OUTCOME_VERSION_MAJOR == 2 && BOOST_OUTCOME_VERSION_MAJOR == 2 &&   \
     BOOST_OUTCOME_VERSION_PATCH > 2)
    virtual _base::payload_info_t payload_info() const noexcept override
    {
        return {
            sizeof(value_type),
            sizeof(status_code_domain *) + sizeof(value_type),
            (alignof(value_type) > alignof(status_code_domain *))
                ? alignof(value_type)
                : alignof(status_code_domain *)};
    }
#endif

protected:
    virtual bool _do_failure(
        const BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code<void> &code)
        const noexcept override
    {
        (void)code;
        assert(code.domain() == *this);
        return true;
    }
    virtual bool _do_equivalent(
        const BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code<void> &code1,
        const BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code<void> &code2)
        const noexcept override
    {
        assert(code1.domain() == *this);
        const auto &c1 = static_cast<const sender_errc_code &>(code1);
        if (code2.domain() == *this) {
            const auto &c2 = static_cast<const sender_errc_code &>(code2);
            return c1.value().code == c2.value().code;
        }
        return false;
    }
    virtual BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::generic_code _generic_code(
        const BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code<void> &code)
        const noexcept override
    {
        (void)code;
        assert(code.domain() == *this);
        return errc::unknown;
    }
    virtual string_ref _do_message(
        const BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code<void> &code)
        const noexcept override
    {
        assert(code.domain() == *this);
        const auto &c = static_cast<const sender_errc_code &>(code);
        switch (sender_errc(c.value().code)) {
        case sender_errc::initiation_immediately_completed:
            return string_ref("initiation_immediately_completed");
        case sender_errc::operation_must_be_reinitiated:
            return string_ref("operation_must_be_reinitiated");
        case sender_errc::unknown:
            break;
        }
        return string_ref("unknown");
    }
    BOOST_OUTCOME_SYSTEM_ERROR2_NORETURN virtual void _do_throw_exception(
        const BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code<void> &code)
        const override
    {
        assert(code.domain() == *this);
        const auto &c = static_cast<const sender_errc_code &>(code);
        throw BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_error<
            _sender_errc_code_domain>(c);
    }
};
constexpr _sender_errc_code_domain sender_errc_code_domain;
inline constexpr const _sender_errc_code_domain &_sender_errc_code_domain::get()
{
    return sender_errc_code_domain;
}
static_assert(
    sizeof(sender_errc_code) <= sizeof(result<void>::error_type),
    "sender_errc_code will not fit inside a result<void>::error_type!");

// ADL discovered customisation point, opts into being able to directly use the
// enum when comparing to status codes.
constexpr inline sender_errc_code make_status_code(sender_errc c)
{
    return sender_errc_code(_sender_errc_code_domain::value_type{c});
}
constexpr inline sender_errc_code
make_status_code(sender_errc c, uintptr_t value)
{
    return sender_errc_code(_sender_errc_code_domain::value_type{c, value});
}

MONAD_ASYNC_NAMESPACE_END
