---
label: Error Code Enum (std::error_code)
order: 40
description: Your own error codes as std::error_code, with a category that gives each one a message
ask: Name = Network
includes: <string> <system_error> <type_traits>
---
// The codes then work like the standard library's: compare with ==, print with message(), return them
// without exceptions.
//     std::error_code error = {{Name}}Error::Timeout;
//     if (error) std::cerr << error.message() << '\n';
enum class {{Name}}Error
{
    // 0 means "no error" to std::error_code, so the codes start at 1.
    NotFound = 1,
    PermissionDenied,
    Timeout,
};

template <>
struct std::is_error_code_enum<{{Name}}Error> : std::true_type
{};

inline const std::error_category& {{Name:camel}}Category()
{
    class Category : public std::error_category
    {
    public:
        const char* name() const noexcept override { return "{{Name:snake}}"; }

        std::string message(int code) const override
        {
            switch (static_cast<{{Name}}Error>(code))
            {
            case {{Name}}Error::NotFound:
                return "not found";
            case {{Name}}Error::PermissionDenied:
                return "permission denied";
            case {{Name}}Error::Timeout:
                return "timed out";
            }
            return "unknown error";
        }
    };
    static const Category category;
    return category;
}

// Must be in the same namespace as the enum: std::error_code finds it through argument-dependent lookup.
inline std::error_code make_error_code({{Name}}Error error)
{
    return {static_cast<int>(error), {{Name:camel}}Category()};
}
