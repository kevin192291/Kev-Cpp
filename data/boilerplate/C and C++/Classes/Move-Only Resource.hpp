---
order: 51
description: A handle that owns a POSIX file descriptor: movable, not copyable, closed exactly once
ask: ClassName = FileDescriptor
includes: <unistd.h> <utility>
---
// Like std::unique_ptr, for a resource that isn't a pointer: it can be moved but not copied, and the
// destructor releases it. For pointers, std::unique_ptr with a custom deleter does the same job.
class {{ClassName}}
{
public:
    {{ClassName}}() = default;
    explicit {{ClassName}}(int fd) : fd_(fd) {}
    ~{{ClassName}}() { reset(); }

    {{ClassName}}(const {{ClassName}}&) = delete;
    {{ClassName}}& operator=(const {{ClassName}}&) = delete;

    {{ClassName}}({{ClassName}}&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

    {{ClassName}}& operator=({{ClassName}}&& other) noexcept
    {
        if (this != &other)
        {
            reset(std::exchange(other.fd_, -1));
        }
        return *this;
    }

    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }

    // Gives up ownership without closing; the caller must close it.
    [[nodiscard]] int release() { return std::exchange(fd_, -1); }

    void reset(int fd = -1)
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = -1;
};
