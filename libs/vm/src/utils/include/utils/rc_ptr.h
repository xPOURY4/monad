#pragma once

#include <memory>
#include <utility>

namespace monad::utils
{
    template <typename T>
    struct RcObject
    {
        size_t ref_count;
        T object;

        static RcObject<T> *default_allocate()
        {
            return std::allocator<RcObject<T>>().allocate(1);
        }

        static void default_deallocate(RcObject<T> *rco)
        {
            std::allocator<RcObject<T>>().deallocate(rco, 1);
        }

        struct DefaultDeallocate
        {
            void operator()(RcObject<T> *rco)
            {
                default_deallocate(rco);
            }
        };
    };

    template <typename T, typename Deallocate>
    class RcPtr
    {
    public:
        template <typename Allocator, typename... Args>
        static RcPtr allocate(Allocator const &allocate, Args &&...args)
        {
            RcPtr p{allocate()};
            p.rc_object->ref_count = 1;
            new (&p.rc_object->object) T(std::forward<Args>(args)...);
            return p;
        }

        RcPtr()
            : rc_object{nullptr}
        {
        }

        RcPtr(std::nullptr_t)
            : rc_object{nullptr}
        {
        }

        RcPtr(RcPtr const &x)
            : rc_object{x.rc_object}
        {
            ++rc_object->ref_count;
        }

        RcPtr(RcPtr &&x)
            : rc_object{x.rc_object}
        {
            x.rc_object = nullptr;
        }

        RcPtr &operator=(RcPtr const &x)
        {
            this->~RcPtr();
            rc_object = x.rc_object;
            ++rc_object->ref_count;
            return *this;
        }

        RcPtr &operator=(RcPtr &&x)
        {
            this->~RcPtr();
            rc_object = x.rc_object;
            x.rc_object = nullptr;
            return *this;
        }

        void reset()
        {
            this->~RcPtr();
            rc_object = nullptr;
        }

        RcPtr &operator=(std::nullptr_t)
        {
            reset();
            return *this;
        }

        ~RcPtr()
        {
            if (rc_object && !--rc_object->ref_count) {
                rc_object->object.~T();
                Deallocate()(rc_object);
            }
        }

        T &operator*() const
        {
            return rc_object->object;
        }

        T *operator->() const
        {
            return &rc_object->object;
        }

        friend bool operator==(RcPtr const &x, RcPtr const &y)
        {
            return x.rc_object == y.rc_object;
        }

        // Note: undefined when RcPtr is nullptr.
        T *get() const
        {
            return &rc_object->object;
        }

        operator bool() const
        {
            return rc_object;
        }

    private:
        RcPtr(RcObject<T> *rco)
            : rc_object{rco}
        {
        }

        RcObject<T> *rc_object;
    };
}
