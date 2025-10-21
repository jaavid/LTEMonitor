#include "credentials.hpp"

#include "Simple-Web-Server/crypto.hpp"

using SimpleWeb::Crypto;

namespace security {

namespace {
std::string hashPassword(const std::string &password) {
    if (password.empty()) {
        return std::string();
    }
    return Crypto::to_hex_string(Crypto::sha256(password));
}
} // namespace

Credentials::Credentials(const std::string &server, const std::string &user, const std::string &password) {
    setServer(server);
    setUser(user);
    setPassword(password);
}

void Credentials::setServer(const std::string &server) {
    server_ = server;
}

void Credentials::setUser(const std::string &user) {
    user_ = user;
}

void Credentials::setPassword(const std::string &password) {
    password_ = password;
    passwordHash_ = hashPassword(password);
}

const std::string &Credentials::server() const noexcept {
    return server_;
}

const std::string &Credentials::user() const noexcept {
    return user_;
}

const std::string &Credentials::password() const noexcept {
    return password_;
}

const std::string &Credentials::passwordHash() const noexcept {
    return passwordHash_;
}

bool Credentials::hasPassword() const noexcept {
    return !password_.empty();
}

std::string Credentials::maskedPassword() const {
    return maskSecret(password_);
}

std::string Credentials::maskSecret(const std::string &secret) {
    if (secret.empty()) {
        return std::string();
    }
    return maskPlaceholder();
}

bool Credentials::isMaskedValue(const std::string &value) {
    return !value.empty() && value == maskPlaceholder();
}

const std::string &Credentials::maskPlaceholder() {
    static const std::string placeholder("********");
    return placeholder;
}

} // namespace security

