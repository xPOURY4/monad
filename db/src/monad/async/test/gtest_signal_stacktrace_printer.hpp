#pragma once

#include <gtest/gtest.h>

#include <monad/core/assert.h>

#include <boost/stacktrace/stacktrace.hpp>

#include <array>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <span>

#include <unistd.h>

namespace monad::test
{
    namespace detail
    {
        template <class T>
        class FixedBufferAllocator
        {
            template <class U>
            friend class FixedBufferAllocator;

            std::span<std::byte> const buffer;
            std::byte *&p;

        public:
            using value_type = T;
            using size_type = std::size_t;
            using difference_type = std::ptrdiff_t;
            using propagate_on_container_move_assignment = std::true_type;
            using is_always_equal = std::true_type;

            constexpr FixedBufferAllocator(
                std::span<std::byte> buffer_, std::byte *&p_)
                : buffer(buffer_)
                , p(p_)
            {
                p = buffer.data();
            }
            template <class U>
            constexpr FixedBufferAllocator(FixedBufferAllocator<U> const &o)
                : buffer(o.buffer)
                , p(o.p)
            {
            }

            [[nodiscard]] constexpr value_type *allocate(std::size_t n)
            {
                auto *newp = p + sizeof(value_type) * n;
                MONAD_ASSERT(size_t(newp - buffer.data()) <= buffer.size());
                auto *ret = (value_type *)p;
                p = newp;
                return ret;
            }
            constexpr void deallocate(value_type *, std::size_t) {}
        };
    }

    struct SignalStackTracePrinterEnvironment final
        : public ::testing::Environment
    {
        static constexpr std::pair<int, std::string_view>
            signals_to_backtrace[] = {
                {SIGABRT, "SIGABRT"},
                {SIGBUS, "SIGBUS"},
                {SIGFPE, "SIGFPE"},
                {SIGILL, "SIGILL"},
                {SIGPIPE, "SIGPIPE"},
                {SIGSEGV, "SIGSEGV"}};
        void SetUp() override
        {
            auto &signal_handlers = this->signal_handlers();
            signal_buffer();
            for (auto const &signo : signals_to_backtrace) {
                struct sigaction sa, oldsa;
                memset(&sa, 0, sizeof(sa));
                memset(&oldsa, 0, sizeof(oldsa));
                sa.sa_sigaction = &signal_handler;
                sa.sa_flags = SA_SIGINFO;
                MONAD_ASSERT(0 == ::sigaction(signo.first, &sa, &oldsa));
                signal_handlers[signo.first] = oldsa;
            }
        }
        void TearDown() override {}

        static std::map<int, struct sigaction> &signal_handlers()
        {
            static std::map<int, struct sigaction> v;
            return v;
        }
        static std::span<std::byte, 16384> signal_buffer()
        {
            static std::array<std::byte, 16384> v;
            return v;
        }
        static void
        signal_handler(int signo, ::siginfo_t *siginfo, void *context) noexcept
        {
            auto const &signal_handlers =
                SignalStackTracePrinterEnvironment::signal_handlers();
            auto const *old_signal_handler = [&]() -> struct sigaction const *
            {
                auto it = signal_handlers.find(signo);
                if (it == signal_handlers.end()) {
                    return nullptr;
                }
                return &it->second;
            }
            ();
            auto write = [](char const *fmt, ...) {
                va_list args;
                va_start(args, fmt);
                char buffer[1024];
                // NOTE: sprintf may call malloc, and is not guaranteed async
                // signal safe. Chances are very good it will be async signal
                // safe for how we're using it here.
                auto written = std::min(
                    size_t(::vsnprintf(buffer, 1024, fmt, args)), size_t(1024));
                ::write(1, buffer, written);
                va_end(args);
            };
            {
                std::byte *signal_buffer_ptr = nullptr;
                using MainAllocatorType = detail::FixedBufferAllocator<char>;
                MainAllocatorType alloc(
                    SignalStackTracePrinterEnvironment::signal_buffer(),
                    signal_buffer_ptr);
                using StacktraceAllocator =
                    std::allocator_traits<MainAllocatorType>::rebind_alloc<
                        ::boost::stacktrace::stacktrace::allocator_type::
                            value_type>;
                ::boost::stacktrace::basic_stacktrace<StacktraceAllocator> st{
                    StacktraceAllocator(alloc)};
                char const *const signame = [](int signo) {
                    for (auto const &i : signals_to_backtrace) {
                        if (signo == i.first) {
                            return i.second.data();
                        }
                    }
                    return "unknown";
                }(signo);
                write(
                    "\nSignal %s (%d) occurred due to address %p:",
                    signame,
                    signo,
                    siginfo->si_addr);
                for (auto const &frame : st) {
                    write("\n   %p", frame.address());
                }
                write("\n\nAttempting async signal unsafe human readable "
                      "stacktrace:");
                for (auto const &frame : st) {
                    write("\n   %p:", frame.address());
                    write(" %s", frame.name().c_str());
                    if (frame.source_line() > 0) {
                        write(
                            "\n                   [%s:%zu]",
                            frame.source_file().c_str(),
                            frame.source_line());
                    }
                }
            }
            write("\n");
            if (old_signal_handler != nullptr) {
                if ((old_signal_handler->sa_flags & SA_SIGINFO) != 0 &&
                    old_signal_handler->sa_sigaction != nullptr) {
                    old_signal_handler->sa_sigaction(signo, siginfo, context);
                }
                else if (old_signal_handler->sa_handler == SIG_IGN) {
                    // it is ignored
                }
                else if (old_signal_handler->sa_handler == SIG_DFL) {
                    // Default action for these is to ignore
                    if (signo == SIGCHLD || signo == SIGURG
#ifdef SIGWINCH
                        || signo == SIGWINCH
#endif
#ifdef SIGINFO
                        || signo == SIGINFO
#endif
                    )
                        return;

                    // Simulate invoke the default handler.
                    // Immediate exit without running cleanup
                    _exit(127); // compatibility code with glibc's default
                                // signal handler
                }
            }
        }
    };
    static auto const *RegisterSignalStackTracePrinterEnvironment =
        ::testing::AddGlobalTestEnvironment(
            new SignalStackTracePrinterEnvironment);
}
