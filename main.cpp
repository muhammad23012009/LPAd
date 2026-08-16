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

#include <sdbus-c++/sdbus-c++.h>
#include <thread>
#include <iostream>
#include <string>
#include <mutex>

#include <lpa.h>
#include <errors.h>

SDBUSCPP_REGISTER_STRUCT(EuiccInfo, osVersion, availableMemory)
SDBUSCPP_REGISTER_STRUCT(EuiccProfile, profileName, serviceProviderName, nickname, iccid, enabled)

static int global_counter = 0;

static LPA* g_lpa = nullptr;
static std::mutex g_mutex;
static std::condition_variable g_cv;
static bool g_confirmed = false;
static EuiccProfile g_profile;

std::unique_ptr<sdbus::IConnection> g_connection;
std::unique_ptr<sdbus::IObject> g_object;

constexpr std::string_view SERVICE_NAME = "com.ubports.lpa";

int _init_libcurl(void);

auto getConnection()
{
    if (!g_connection)
    {
        sdbus::ServiceName name{"com.ubports.lpa"};
        g_connection = sdbus::createBusConnection(name);
    }

    return g_connection.get();
}

template <typename Functor>
class Signature;

template <typename Ret, typename... Args>
class Signature<Ret(Args...)>
{
public:
    using arguments = std::tuple<std::decay_t<Args>...>;
    using return_type = std::decay_t<Ret>;
};

template <typename Ret, typename Class, typename... Args>
class Signature<Ret(Class::*)(Args...)>
{
public:
    using arguments = std::tuple<std::decay_t<Args>...>;
    using return_type = std::decay_t<Ret>;
};

template <typename T>
struct ShowType;

template <typename Ptr, typename Func>
auto invoke(Ptr ptr, Func&& func)
{
    return [ptr, func = std::forward<Func>(func)](sdbus::MethodCall call) {
        std::thread([ptr, func = std::move(func), call = std::move(call)]() mutable {
            try {
                using Args = Signature<decltype(func)>::arguments;
                using Ret = Signature<decltype(func)>::return_type;
                Args funcArgs;

                auto deserialize_arguments = [&]<std::size_t... I>(std::index_sequence<I...>) constexpr {
                    ([&]() constexpr {
                        using Type = std::tuple_element_t<I, Args>;
                        Type value;
                        call >> value;
                        std::get<I>(funcArgs) = std::move(value);
                    }(), ...);
                };

                auto call_func = [&]<std::size_t... I>(std::index_sequence<I...>) constexpr {
                    return std::invoke(func, ptr, std::get<I>(funcArgs)...);
                };

                constexpr auto arg_count = std::tuple_size_v<Args>;
                constexpr auto sequence = std::make_index_sequence<arg_count>{};

                deserialize_arguments(sequence);

                auto reply = call.createReply();
                if constexpr (!std::is_same_v<void, Ret>)
                {
                    auto result = call_func(sequence);
                    reply << result;
                } else
                    call_func(sequence);

                reply.send();
            } catch (const std::exception& e) {
                auto error = sdbus::Error{sdbus::Error::Name{"com.ubports.lpa.Error"}, e.what()};
                call.createErrorReply(error).send();
                return;
            }
        }).detach();
    };
}

void installProfileHandler(sdbus::MethodCall call)
{
    sdbus::ObjectPath path{"/com/ubports/lpa/install/" + std::to_string(global_counter++)};
    auto object = sdbus::createObject(*getConnection(), path);

    object->addVTable(sdbus::InterfaceName{"com.ubports.lpa.Install"}, {
        sdbus::MethodVTableItem{sdbus::MethodName{"ProfileInfo"}, sdbus::Signature{""}, {}, sdbus::Signature{"(ssssb)"}, {}, [](sdbus::MethodCall call) {
            auto reply = call.createReply();
            reply << g_profile;
            reply.send();
        }, {}},
        sdbus::MethodVTableItem{sdbus::MethodName{"ConfirmInstall"}, sdbus::Signature{"b"}, {}, sdbus::Signature{""}, {}, [](sdbus::MethodCall call) {
            bool confirmed;
            call >> confirmed;

            g_confirmed = confirmed;

            call.createReply().send();

            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.notify_all();
        }, {}}
    });

    std::string smdp, activationCode, confirmationCode;
    call >> smdp >> activationCode >> confirmationCode;

    std::thread([object = std::move(object), path, smdp, activationCode, confirmationCode]() mutable {
        try {
            g_lpa->installProfile(smdp, activationCode, confirmationCode, [](EuiccProfile profile) -> bool {
                std::unique_lock<std::mutex> lock(g_mutex);
                g_profile = profile;
                g_cv.wait(lock);

                auto ret = g_confirmed;
                g_confirmed = false;

                return ret;
            });
        } catch (const LPAException& e) {
            auto signal = g_object->createSignal(sdbus::InterfaceName{"com.ubports.lpa"}, sdbus::MethodName{"InstallError"});
            signal << path << e.type_to_string() << e.what();
            g_object->emitSignal(signal);
        }
    }).detach();

    auto reply = call.createReply();
    reply << path;
    reply.send();
}

int main(void)
{
    _init_libcurl();
    g_lpa = new LPA();
    auto connection = getConnection();

    sdbus::ObjectPath path{"/com/ubports/lpa"};
    g_object = sdbus::createObject(*connection, path);

    sdbus::InterfaceName interface{"com.ubports.lpa"};
    g_object->addVTable(interface, {
        sdbus::MethodVTableItem{sdbus::MethodName{"GetEid"}, sdbus::Signature{""}, {}, sdbus::Signature{"s"}, {}, invoke(g_lpa, &LPA::getEid), {}},
        sdbus::MethodVTableItem{sdbus::MethodName{"GetEuiccInfo"}, sdbus::Signature{""}, {}, sdbus::Signature{"(si)"}, {}, invoke(g_lpa, &LPA::getEuiccInfo), {}},
        sdbus::MethodVTableItem{sdbus::MethodName{"GetProfiles"}, sdbus::Signature{""}, {}, sdbus::Signature{"a(ssssb)"}, {}, invoke(g_lpa, &LPA::getProfiles), {}},
        sdbus::MethodVTableItem{sdbus::MethodName{"EnableProfile"}, sdbus::Signature{"s"}, {}, sdbus::Signature{""}, {}, invoke(g_lpa, &LPA::enableProfile), {}},
        sdbus::MethodVTableItem{sdbus::MethodName{"DisableProfile"}, sdbus::Signature{"s"}, {}, sdbus::Signature{""}, {}, invoke(g_lpa, &LPA::disableProfile), {}},
        sdbus::MethodVTableItem{sdbus::MethodName{"InstallProfile"}, sdbus::Signature{"sss"}, {}, sdbus::Signature{"o"}, {}, installProfileHandler, {}},
        sdbus::MethodVTableItem{sdbus::MethodName{"RemoveProfile"}, sdbus::Signature{"s"}, {}, sdbus::Signature{""}, {}, invoke(g_lpa, &LPA::removeProfile), {}},
    });

    g_lpa->addProfileChangedCallback([](EuiccProfile profile) {
        auto signal = g_object->createSignal(sdbus::InterfaceName{"com.ubports.lpa"}, sdbus::MethodName{"ProfileChanged"});
        signal << profile;
        g_object->emitSignal(signal);
    });

    connection->enterEventLoop();

    delete g_lpa;

    return 0;
}
