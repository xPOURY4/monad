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

class sender_errc_code_domain_;
using sender_errc_code = BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code<
    sender_errc_code_domain_>;
class sender_errc_code_domain_
    : public BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code_domain
{
    friend sender_errc_code;
    using base_ = BOOST_OUTCOME_SYSTEM_ERROR2_NAMESPACE::status_code_domain;

public:
    struct value_type
    {
        static constexpr auto value_bits = (8 * sizeof(uintptr_t) - 8);
        static_assert(value_bits == 56);

        uintptr_t code : 8; // sender_errc
        uintptr_t value : value_bits;

        static constexpr uintptr_t max_value = (1ULL << value_bits) - 1;

        constexpr value_type() noexcept
            : code(0)
            , value(0)
        {
        }
        constexpr value_type(sender_errc code_) noexcept
            : code(uint8_t(code_))
            , value(0)
        {
        }
        constexpr value_type(sender_errc code_, uintptr_t value_) noexcept
            : code(uint8_t(code_))
            , value(value_ & max_value)
        {
            assert(value_ <= max_value);
        }
    };
    using base_::string_ref;

    constexpr explicit sender_errc_code_domain_(
        typename base_::unique_id_type id = 0xa88a5a64a7d218d8) noexcept
        : base_(id)
    {
    }
    sender_errc_code_domain_(const sender_errc_code_domain_ &) = default;
    sender_errc_code_domain_(sender_errc_code_domain_ &&) = default;
    sender_errc_code_domain_ &
    operator=(const sender_errc_code_domain_ &) = default;
    sender_errc_code_domain_ &operator=(sender_errc_code_domain_ &&) = default;
    ~sender_errc_code_domain_() = default;

    static inline constexpr const sender_errc_code_domain_ &get();

    virtual string_ref name() const noexcept override
    {
        return string_ref("sender_errc domain");
    }

#if BOOST_OUTCOME_VERSION_MAJOR > 2 ||                                         \
    (BOOST_OUTCOME_VERSION_MAJOR == 2 && BOOST_OUTCOME_VERSION_MINOR > 2) ||   \
    (BOOST_OUTCOME_VERSION_MAJOR == 2 && BOOST_OUTCOME_VERSION_MAJOR == 2 &&   \
     BOOST_OUTCOME_VERSION_PATCH > 2)
    virtual base_::payload_info_t payload_info() const noexcept override
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
        auto const &c1 = static_cast<sender_errc_code const &>(code1);
        if (code2.domain() == *this) {
            auto const &c2 = static_cast<sender_errc_code const &>(code2);
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
        auto const &c = static_cast<sender_errc_code const &>(code);
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
            sender_errc_code_domain_>(c);
    }
};
constexpr sender_errc_code_domain_ sender_errc_code_domain;
inline constexpr const sender_errc_code_domain_ &sender_errc_code_domain_::get()
{
    return sender_errc_code_domain;
}
static_assert(
    sizeof(sender_errc_code) <= sizeof(result<void>::error_type),
    "sender_errc_code will not fit inside a result<void>::error_type!");

// ADL discovered customisation point, opts into being able to directly use the
// enum when comparing to status codes.
inline constexpr sender_errc_code make_status_code(sender_errc c)
{
    return sender_errc_code(sender_errc_code_domain_::value_type{c});
}
inline constexpr sender_errc_code
make_status_code(sender_errc c, uintptr_t value)
{
    return sender_errc_code(sender_errc_code_domain_::value_type{c, value});
}

MONAD_ASYNC_NAMESPACE_END
