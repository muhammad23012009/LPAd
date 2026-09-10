/*
 * This file is part of LPAd (https://github.com/muhammad23012009/LPAd)
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
#include <memory>

#include <lpa.h>
#include <errors.h>

#include <driver_interface.h>
#include <modem_interface.h>

#include "drivers/gbinder/gbinder.hpp"
#include "threadpool.h"

SDBUSCPP_REGISTER_STRUCT(EuiccInfo, osVersion, availableMemory)
SDBUSCPP_REGISTER_STRUCT(EuiccProfile, profileName, serviceProviderName, nickname, iccid, enabled)
SDBUSCPP_REGISTER_STRUCT(PortInfo, portId, portType, currentPort)
SDBUSCPP_REGISTER_STRUCT(SlotInfo, slotId, ports)

static int global_counter = 0;

static DriverInterface* g_driver;
static std::unique_ptr<ThreadPool> g_threadPool;

std::unique_ptr<sdbus::IObject> g_object;

constexpr std::string_view SERVICE_NAME = "com.ubports.lpa";

int _init_libcurl(void);

auto getConnection()
{
    static std::unique_ptr<sdbus::IConnection> connection;
    if (!connection)
    {
        sdbus::ServiceName name{"com.ubports.lpa"};
        connection = sdbus::createBusConnection(name);
    }

    return connection.get();
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

template <typename Ret, typename Class, typename... Args>
class Signature<Ret(Class::*)(Args...) const>
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
        g_threadPool->post([ptr, func = std::move(func), call = std::move(call)]() mutable {
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
        });
    };
}

auto installProfileHandler(LPA* lpa)
{
    return [lpa](sdbus::MethodCall call) {
        sdbus::ObjectPath path{"/com/ubports/lpa/install/" + std::to_string(global_counter++)};
        auto object = sdbus::createObject(*getConnection(), path);

        object->addVTable(sdbus::InterfaceName{"com.ubports.lpa.Install"}, {
            sdbus::MethodVTableItem{sdbus::MethodName{"ProfileInfo"}, sdbus::Signature{""}, {}, sdbus::Signature{"(ssssb)"}, {}, [lpa](sdbus::MethodCall call) {
                auto reply = call.createReply();
                reply << lpa->getPendingProfile().value();
                reply.send();
            }, {}},
            sdbus::MethodVTableItem{sdbus::MethodName{"ConfirmInstall"}, sdbus::Signature{"b"}, {}, sdbus::Signature{""}, {}, [lpa](sdbus::MethodCall call) {
                bool confirmed;
                call >> confirmed;

                lpa->confirmInstall(confirmed);

                call.createReply().send();
            }, {}}
        });

        std::string smdp, activationCode, confirmationCode;
        call >> smdp >> activationCode >> confirmationCode;

        g_threadPool->post([object = object.release(), path, smdp, activationCode, confirmationCode, lpa]() mutable {
            try {
                lpa->installProfile(smdp, activationCode, confirmationCode);
            } catch (const LPAException& e) {
                auto signal = g_object->createSignal(sdbus::InterfaceName{"com.ubports.lpa"}, sdbus::MethodName{"InstallError"});
                signal << path << e.type_to_string() << e.what();
                g_object->emitSignal(signal);
            }

            delete object;
        });

        auto reply = call.createReply();
        reply << path;
        reply.send();
    };
}

int main(void)
{
    _init_libcurl();
    std::vector<std::string> modemPaths;
    std::vector<std::unique_ptr<sdbus::IObject>> modemObjects;
    std::vector<std::unique_ptr<sdbus::IObject>> euiccObjects;
    std::vector<std::unique_ptr<LPA>> lpaInstances;

    if (GBinderDriver::usable())
    {
        g_driver = new GBinderDriver();
    }
    else
    {
        std::cerr << "No usable driver found" << std::endl;
        return 1;
    }

    // TODO: add some way to make sure the dbus signatures never deviate from the actual method signatures
    auto connection = getConnection();
    g_threadPool = std::make_unique<ThreadPool>(4);

    auto createEuiccInterface = [&euiccObjects, &lpaInstances](const std::string& modemPath, std::shared_ptr<EuiccInterface> euiccInterface)
    {
        sdbus::ObjectPath euiccPath{modemPath + "/slot" + std::to_string(euiccInterface->slotId())};
        auto object = sdbus::createObject(*getConnection(), euiccPath);
        auto lpa = std::make_unique<LPA>(euiccInterface);
        sdbus::InterfaceName interface{"com.ubports.lpa.Euicc"};

        object->addVTable(interface, {
            sdbus::MethodVTableItem{sdbus::MethodName{"GetEid"}, sdbus::Signature{""}, {}, sdbus::Signature{"s"}, {}, invoke(lpa.get(), &LPA::getEid), {}},
            sdbus::MethodVTableItem{sdbus::MethodName{"GetEuiccInfo"}, sdbus::Signature{""}, {}, sdbus::Signature{"(si)"}, {}, invoke(lpa.get(), &LPA::getEuiccInfo), {}},
            sdbus::MethodVTableItem{sdbus::MethodName{"GetProfiles"}, sdbus::Signature{""}, {}, sdbus::Signature{"a(ssssb)"}, {}, invoke(lpa.get(), &LPA::getProfiles), {}},
            sdbus::MethodVTableItem{sdbus::MethodName{"EnableProfile"}, sdbus::Signature{"s"}, {}, sdbus::Signature{""}, {}, invoke(lpa.get(), &LPA::enableProfile), {}},
            sdbus::MethodVTableItem{sdbus::MethodName{"DisableProfile"}, sdbus::Signature{"s"}, {}, sdbus::Signature{""}, {}, invoke(lpa.get(), &LPA::disableProfile), {}},
            sdbus::MethodVTableItem{sdbus::MethodName{"InstallProfile"}, sdbus::Signature{"sss"}, {}, sdbus::Signature{"o"}, {}, installProfileHandler(lpa.get()), {}},
            sdbus::MethodVTableItem{sdbus::MethodName{"RemoveProfile"}, sdbus::Signature{"s"}, {}, sdbus::Signature{""}, {}, invoke(lpa.get(), &LPA::removeProfile), {}},
        });

        if (euiccInterface->slotId() >= euiccObjects.capacity())
        {
            euiccObjects.resize(euiccInterface->slotId() + 1);
            lpaInstances.resize(euiccInterface->slotId() + 1);
        }

        euiccObjects[euiccInterface->slotId()] = std::move(object);
        lpaInstances[euiccInterface->slotId()] = std::move(lpa);
    };

    for (const auto& modem : g_driver->getModems())
    {
        sdbus::ObjectPath path{"/com/ubports/lpa/" + modem->modemName()};
        modemPaths.push_back(path.c_str());
        auto object = sdbus::createObject(*connection, path);

        sdbus::InterfaceName interface{"com.ubports.lpa.Modem"};
        object->addVTable(interface, {
            sdbus::MethodVTableItem{sdbus::MethodName{"GetMEPMode"}, sdbus::Signature{""}, {}, sdbus::Signature{"i"}, {}, invoke(modem.get(), &ModemInterface::supportedMEPMode), {}},
            sdbus::MethodVTableItem{sdbus::MethodName{"GetPhysicalSlots"}, sdbus::Signature{""}, {}, sdbus::Signature{"a(ia(iib))"}, {}, invoke(modem.get(), &ModemInterface::physicalSlots), {}},
            sdbus::MethodVTableItem{sdbus::MethodName{"SetPortMapping"}, sdbus::Signature{"ii"}, {}, sdbus::Signature{""}, {}, invoke(modem.get(), &ModemInterface::setPortMapping), {}},
        });
        modemObjects.push_back(std::move(object));

        modem->addEuiccInterfacesChangedCallbacks(
            [createEuiccInterface, &euiccObjects, &lpaInstances, path](auto euiccInterface) {
                createEuiccInterface(path.c_str(), euiccInterface);
            },
            [&euiccObjects, &lpaInstances, path](int slotId) {
                euiccObjects.erase(euiccObjects.begin() + slotId);
                lpaInstances.erase(lpaInstances.begin() + slotId);
            }
        );

        euiccObjects.resize(modem->euiccInterfaces().size() + 1);
        lpaInstances.resize(modem->euiccInterfaces().size() + 1);

        for (const auto& euicc : modem->euiccInterfaces())
        {
            createEuiccInterface(path.c_str(), euicc);
        }
    }


    sdbus::ObjectPath path{"/com/ubports/lpa"};
    g_object = sdbus::createObject(*connection, path);

    sdbus::InterfaceName interface{"com.ubports.lpa"};
    g_object->addVTable(interface, {
        sdbus::MethodVTableItem{sdbus::MethodName{"GetModems"}, sdbus::Signature{""}, {}, sdbus::Signature{"ao"}, {}, [modemPaths](sdbus::MethodCall call) {
            auto reply = call.createReply();
            std::vector<sdbus::ObjectPath> paths;
            for (const auto& modemPath : modemPaths)
            {
                paths.emplace_back(modemPath);
            }
            reply << paths;
            reply.send();
        }, {}},
    });

    connection->enterEventLoop();

    return 0;
}
