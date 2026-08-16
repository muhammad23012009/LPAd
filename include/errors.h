/*
 * This file is part of lpaD (https://github.com/muhammad23012009/lpaD)
 * Copyright (c) 2026 Muhammad Asif  <thevancedgamer@mentallysanemainliners.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ERRORS_H
#define ERRORS_H

#include <stdexcept>

class LPAException : public std::runtime_error
{
public:
    enum class ErrorType {
        ERROR,
        EUICC_CHALLENGE_ERROR,
        AUTHENTICATION_INIT_ERROR,
        SERVER_AUTH_ERROR,
        CLIENT_AUTH_ERROR,
        PROFILE_DOWNLOAD_ERROR,
        PROFILE_INSTALL_ERROR,
        PROFILE_INSTALL_REJECTED,
        PROFILE_ENABLE_ERROR,
        PROFILE_DISABLE_ERROR,
        PROFILE_DELETE_ERROR,
    };

    explicit LPAException(ErrorType type, const std::string& message = std::string()):
      std::runtime_error(message),
      m_type(type)
    {}

    ErrorType type() const { return m_type; }
    std::string type_to_string() const {
        switch (m_type) {
            case ErrorType::EUICC_CHALLENGE_ERROR:
                return "ChallengeError";
            case ErrorType::AUTHENTICATION_INIT_ERROR:
                return "AuthenticationInitError";
            case ErrorType::SERVER_AUTH_ERROR:
                return "ServerAuthError";
            case ErrorType::CLIENT_AUTH_ERROR:
                return "ClientAuthError";
            case ErrorType::PROFILE_DOWNLOAD_ERROR:
                return "ProfileDownloadError";
            case ErrorType::PROFILE_INSTALL_ERROR:
                return "ProfileInstallError";
            case ErrorType::PROFILE_INSTALL_REJECTED:
                return "ProfileInstallRejected";
            case ErrorType::PROFILE_ENABLE_ERROR:
                return "ProfileEnableError";
            case ErrorType::PROFILE_DISABLE_ERROR:
                return "ProfileDisableError";
            case ErrorType::PROFILE_DELETE_ERROR:
                return "ProfileDeleteError";
            default:
                return "Error";
        }
    }
private:
    ErrorType m_type;
};

#endif
