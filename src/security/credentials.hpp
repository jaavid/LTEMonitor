#pragma once

#include <string>

namespace security {

class Credentials {
public:
    Credentials() = default;
    Credentials(const std::string &server, const std::string &user, const std::string &password);

    void setServer(const std::string &server);
    void setUser(const std::string &user);
    void setPassword(const std::string &password);

    const std::string &server() const noexcept;
    const std::string &user() const noexcept;
    const std::string &password() const noexcept;
    const std::string &passwordHash() const noexcept;
    bool hasPassword() const noexcept;

    std::string maskedPassword() const;

    static std::string maskSecret(const std::string &secret);
    static bool isMaskedValue(const std::string &value);

private:
    static const std::string &maskPlaceholder();

    std::string server_;
    std::string user_;
    std::string password_;
    std::string passwordHash_;
};

} // namespace security

