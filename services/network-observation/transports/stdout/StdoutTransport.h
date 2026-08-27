//
// Created by vvass on 21-Jul-26.
//
/**
 * @file StdoutTransport.h
 * @brief Stdout transport — logs entity notifications to stdout.
 */
#pragma once
#include "ITransport.h"

namespace RSCGroup {

class StdoutTransport : public ITransport {
public:
    bool start() override;
    void stop() override;

    void publishLocalStateChanged() override;
    void publishInterfaceChanged(const std::string& ifname) override;
    void publishInterfaceRemoved(const std::string& ifname) override;
    void publishCandidateChanged(const std::string& mac) override;
    void publishCandidateRemoved(const std::string& mac) override;
    void publishReadyChanged(bool ready) override;

private:
    bool running_ = false;
};

} // namespace RSCGroup