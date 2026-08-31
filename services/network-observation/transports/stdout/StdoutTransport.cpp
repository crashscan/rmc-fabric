#include "StdoutTransport.h"

#include <iostream>

namespace RSCGroup {

void StdoutTransport::bindQueryService(IObservationQueryService& provider)
{
    (void)provider;
}

bool StdoutTransport::start()
{
    running_ = true;
    return true;
}

void StdoutTransport::stop()
{
    running_ = false;
}

std::string StdoutTransport::name() const
{
    return "stdout";
}

void StdoutTransport::publishLocalStateChanged()
{
    if (!running_) return;
    std::cout << "[transport] LocalStateChanged" << std::endl;
}

void StdoutTransport::publishInterfaceChanged(const std::string& ifname)
{
    if (!running_) return;
    std::cout << "[transport] InterfaceChanged: " << ifname << std::endl;
}

void StdoutTransport::publishInterfaceRemoved(const std::string& ifname)
{
    if (!running_) return;
    std::cout << "[transport] InterfaceRemoved: " << ifname << std::endl;
}

void StdoutTransport::publishCandidateChanged(const std::string& mac)
{
    if (!running_) return;
    std::cout << "[transport] CandidateChanged: " << mac << std::endl;
}

void StdoutTransport::publishCandidateRemoved(const std::string& mac)
{
    if (!running_) return;
    std::cout << "[transport] CandidateRemoved: " << mac << std::endl;
}

void StdoutTransport::publishReadyChanged(bool ready)
{
    if (!running_) return;
    std::cout << "[transport] ReadyChanged: ready=" << (ready ? "true" : "false") << std::endl;
}

} // namespace RSCGroup
