#pragma once

// A static that is constructed on first use and never destroyed. Process-wide state that a
// detached thread may still touch while the process exits (an NF's NRF lifecycle thread is
// detached; `main` returning runs static destructors) must outlive those threads: a destroyed
// std::mutex or std::shared_ptr there is a use-after-free (ASan on CI, SmfNiddIntegration via the
// PCF, 2026-10-18). The object lives in static storage, so it is still reachable at exit and not a
// leak to LeakSanitizer. Private to sbi-core.

#include <memory>
#include <utility>

namespace sbi_core::detail {

template <class T> class NoDestroy {
public:
    template <class... Args> explicit NoDestroy(Args&&... args) {
        std::construct_at(&value_, std::forward<Args>(args)...);
    }
    NoDestroy(const NoDestroy&) = delete;
    NoDestroy& operator=(const NoDestroy&) = delete;
    // Deliberately empty: value_ is never destroyed.
    ~NoDestroy() {}

    T& get() { return value_; }

private:
    union {
        T value_;
    };
};

} // namespace sbi_core::detail
