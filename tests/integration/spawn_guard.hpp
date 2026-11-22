#pragma once

// Task #170: stop timed-out tests from leaving orphaned NF processes squatting their ports.
//
// Every integration test here spawns real NF binaries (nrf, udr, udm, ...) with fork()+execl() and
// tears them down with SIGTERM+waitpid at the end of the test. That teardown is correct for every
// path the test binary itself controls -- but it cannot run when ctest kills the test binary with
// SIGKILL on --timeout. SIGKILL is not catchable, so no atexit handler, no RAII destructor, and no
// signal handler in this process gets a chance to reap the children. The result, hit repeatedly in
// practice: a timed-out run leaves nrf/udr alive holding their listen ports, and every subsequent
// run of any test fails with "Address already in use" until someone kills them by hand.
//
// The fix has to come from the kernel, because only the kernel observes the parent's death on the
// SIGKILL path. PR_SET_PDEATHSIG asks the kernel to deliver a signal to THIS process when its
// parent dies, for any reason including SIGKILL.
//
// Two portability facts this depends on, both real and both load-bearing:
//   - PR_SET_PDEATHSIG is cleared across fork(), which is why it must be set in the child rather
//     than inherited from the test binary.
//   - It SURVIVES execve() for ordinary binaries (it is reset only when exec'ing a setuid/setgid
//     or file-capability binary). The NF binaries are ordinary, so the disposition set here is
//     still in force once execl() has replaced this image with the NF.
//
// Known residual race, disclosed rather than engineered around: if the parent dies in the window
// between fork() returning and the first getppid() below, the re-check cannot detect it and the
// child survives. That window is microseconds wide, and its failure mode is exactly today's
// behaviour -- so this is a strict improvement, not a complete guarantee.
//
// Known scope limit, also disclosed: this ties an NF's lifetime to the TEST BINARY, not to the
// individual test case. A GoogleTest ASSERT_* that returns early from the middle of a test body
// still skips that test's own SIGTERM/waitpid teardown and leaks the NF until the binary exits;
// that is task #166's RAII-cleanup territory and is deliberately left open here. NFs started
// manually outside ctest are likewise unaffected.

#include <algorithm>
#include <arpa/inet.h>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <netinet/in.h>
#include <string_view>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>

namespace nf_test {

// Call in the CHILD, after fork() and before exec. Only async-signal-safe calls are used
// (getppid/prctl/_exit), so this is legal in the child of a multithreaded parent.
inline void arm_parent_death_signal() {
    const pid_t parent_before = getppid();
    if (parent_before <= 1) {
        _exit(127); // already reparented -- the test binary is gone, so there is nothing to serve
    }
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() != parent_before) {
        _exit(127); // parent died inside the fork/prctl window; the signal will never arrive
    }
}

// RAII wrapper for a spawned NF: SIGTERM + waitpid in the destructor, so teardown runs on EVERY
// exit path out of a test body.
//
// This closes the other half of the orphan problem (task #166). arm_parent_death_signal() above
// covers the test BINARY dying; it does nothing for a test that returns early while the binary
// keeps running -- and a GoogleTest ASSERT_* is exactly that: a `return` from the middle of the
// test body. A test that spawns NFs and tears them down with a kill/waitpid at the END of the
// body therefore skips that teardown entirely the moment any ASSERT_* fires, leaking every NF it
// started and leaving them holding their listen ports for the rest of the run. Declaring these as
// locals fixes it structurally: destruction is guaranteed, and happens in reverse declaration
// order, which matches the usual spawn order (nrf, udr, udm -> torn down udm, udr, nrf).
//
// Same real precedent this generalises: test_udr_ondatachange_webhook.cpp introduced this exact
// pattern after a real early-returning ASSERT there orphaned nrf/udr during development.
class SpawnedProcess {
public:
    // ADR-0295: `log_path`, when given, redirects the child's stdout AND stderr into that file, so
    // a test can assert on what the NF itself reported. That is the only observable some real
    // behaviour has -- an SS7 message DROPPED at a TPS ceiling produces no reply by design, so
    // "nothing came back" is indistinguishable from a malformed message being ignored. The log
    // line is the difference. Same evidence standard ADR-0269/ADR-0270 already used by reading
    // AMF's and UPF's own logs, just no longer by hand.
    explicit SpawnedProcess(const char* path, const char* log_path = nullptr) {
        pid_ = fork();
        if (pid_ == 0) {
            arm_parent_death_signal();
            if (log_path != nullptr) {
                // open/dup2/close are async-signal-safe, which is what makes them legal here.
                const int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd >= 0) {
                    dup2(fd, STDOUT_FILENO);
                    dup2(fd, STDERR_FILENO);
                    if (fd > STDERR_FILENO) {
                        close(fd);
                    }
                }
            }
            execl(path, path, static_cast<char*>(nullptr));
            _exit(127); // only reached if execl fails
        }
    }

    ~SpawnedProcess() { reap(); }

    SpawnedProcess(const SpawnedProcess&) = delete;
    SpawnedProcess& operator=(const SpawnedProcess&) = delete;

    // Movable (but not copyable): several tests group their NFs in a small holder struct returned
    // by value from a spawn_all()-style helper, which needs an accessible move constructor. The
    // moved-from object gives up ownership (pid_ = -1) so only one destructor ever reaps a pid.
    SpawnedProcess(SpawnedProcess&& other) noexcept : pid_(other.pid_) { other.pid_ = -1; }

    SpawnedProcess& operator=(SpawnedProcess&& other) noexcept {
        if (this != &other) {
            reap();
            pid_ = other.pid_;
            other.pid_ = -1;
        }
        return *this;
    }

    pid_t pid() const { return pid_; }

    // For a child expected to run to completion rather than be terminated (e.g. hello-nf, whose
    // test asserts on WIFEXITED/WEXITSTATUS): waits for it and returns the raw wait status, then
    // drops ownership so the destructor does not SIGTERM an already-exited, already-reaped pid.
    int wait_for_exit() {
        int status = 0;
        if (pid_ > 0) {
            waitpid(pid_, &status, 0);
            pid_ = -1;
        }
        return status;
    }

private:
    void reap() {
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            waitpid(pid_, nullptr, 0);
        }
        pid_ = -1;
    }

    pid_t pid_ = -1;
};

// ADR-0617: how long a test waits for a spawned NF to come up, as a deadline rather than an
// attempt count, scaled for builds that start NFs slowly.
//
// Why a scale at all: under `sanitize (asan-ubsan)` on the lab runners (~7 GB RAM, a few hundred
// MB free while nine ASan NF binaries of 150-400 MB each are paged in at once) the UDR has been
// seen to log "starting" ~8 s after it was spawned and "listening" ~10.3 s after -- just past the
// ~10 s every AMF/LI test allowed it, so the test gave up a few tens of milliseconds before the NF
// it was waiting for answered. Plain builds start the same NF in well under a second, so the base
// bounds below are the ones the tests always had; only instrumented builds get longer.
//
//   * TEST_READINESS_TIMEOUT_SCALE (a positive number, e.g. "2" or "0.5") overrides everything,
//     for a developer on a slow machine or a CI leg that wants a different margin;
//   * otherwise a ThreadSanitizer build waits 5x and an AddressSanitizer build 3x the base;
//   * otherwise 1x.
//
// The scale only lengthens how long a test waits for an NF to start. It changes no assertion
// about what the NF then does, and a passing run is no slower: the wait returns the moment the
// probe succeeds.
inline constexpr double kSanitizerReadinessScale =
#if defined(__SANITIZE_THREAD__)
    5.0;
#elif defined(__SANITIZE_ADDRESS__)
    3.0;
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
    5.0;
#elif __has_feature(address_sanitizer)
    3.0;
#else
    1.0;
#endif
#else
    1.0;
#endif

inline double readiness_timeout_scale() {
    if (const char* env = std::getenv("TEST_READINESS_TIMEOUT_SCALE"); env != nullptr) {
        const std::string_view text(env);
        double value = 0.0;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec == std::errc() && end == text.data() + text.size() && value > 0.0) {
            return value;
        }
        // Not a positive number: ignored rather than fatal, so a typo cannot make every wait 0 s.
    }
    return kSanitizerReadinessScale;
}

inline std::chrono::milliseconds readiness_timeout(std::chrono::milliseconds base) {
    return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(
        static_cast<double>(base.count()) * readiness_timeout_scale()));
}

// Calls `probe` until it returns true or readiness_timeout(base_timeout) has elapsed, sleeping
// `poll` between calls. A deadline rather than an attempt count, so the bound holds whatever each
// probe costs: a refused connect returns at once, a half-up TLS listener can take far longer, and
// an attempt count means a different wall-clock bound in each case. The probe is always tried
// once more at the deadline, so an NF that comes up during the last sleep is not missed.
template <typename Probe>
bool wait_until_ready(Probe&& probe,
                      std::chrono::milliseconds base_timeout,
                      std::chrono::milliseconds poll = std::chrono::milliseconds(100)) {
    const auto deadline = std::chrono::steady_clock::now() + readiness_timeout(base_timeout);
    for (;;) {
        if (probe()) {
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(
            std::min<std::chrono::steady_clock::duration>(poll, deadline - now));
    }
}

// True once something accepts TCP connections on 127.0.0.1:port. For NFs that only bind after
// startup work completes -- e.g. the UDR binds after seeding the AKA test subscribers the UDM
// reads (ADR-0383) -- this is a readiness check without needing an SBI client.
//
// `max_attempts` keeps its old meaning as a base bound of max_attempts x 100 ms (10 s by
// default), now scaled per ADR-0617 -- so every existing caller gets the sanitizer margin
// unchanged.
inline bool wait_tcp_listening(unsigned short port, int max_attempts = 100) {
    const auto accepts = [port] {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return false;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const bool ok = ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
        ::close(fd);
        return ok;
    };
    return wait_until_ready(accepts, std::chrono::milliseconds(100) * max_attempts);
}

} // namespace nf_test
