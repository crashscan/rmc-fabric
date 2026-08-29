#include "ServiceTransportFactory.h"

#include <ITransport.h>
#include <glog/logging.h>

namespace RSCGroup {

// static
std::unordered_map<std::string, ServiceTransportFactory::Builder>&
ServiceTransportFactory::registry()
{
    static std::unordered_map<std::string, Builder> instance;
    return instance;
}

// static
void ServiceTransportFactory::registerBuilder(const std::string& name, Builder builder)
{
    registry()[name] = std::move(builder);
}

// static
std::shared_ptr<IServiceTransport> ServiceTransportFactory::create(
    const std::string& name, const std::string& config)
{
    auto& reg = registry();
    auto it = reg.find(name);
    if (it == reg.end()) {
        LOG(ERROR) << "ServiceTransportFactory: unknown transport '" << name << "'";
        return nullptr;
    }
    return it->second(config);
}

// static
bool ServiceTransportFactory::hasBuilder(const std::string& name)
{
    return registry().count(name) != 0;
}

// static
void ServiceTransportFactory::clear()
{
    registry().clear();
}

} // namespace RSCGroup
