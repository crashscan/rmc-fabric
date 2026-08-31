//
// Created by vvass on 24-Jul-26.
//
#include "LldpObserver.h"

namespace RSCGroup {

class LldpObserver::Impl {
public:
    explicit Impl(std::unique_ptr<ILldpSource> source)
        : source_(std::move(source)) {}

    bool start() { return source_->start(); }
    void stop() { source_->stop(); }
    bool isRunning() const { return source_->isRunning(); }

    void refreshAll() { source_->refreshAll(); }
    void refreshInterface(const std::string& ifname) { source_->refreshInterface(ifname); }

    void onInterfaceUp(const std::string& ifname) {
        source_->refreshInterface(ifname);
    }

    void onInterfaceDown(const std::string& ifname) {
        source_->removeInterface(ifname);
    }

    void onInterfaceRemoved(const std::string& ifname) {
        source_->removeInterface(ifname);
    }

private:
    std::unique_ptr<ILldpSource> source_;
};

LldpObserver::LldpObserver(std::unique_ptr<ILldpSource> source)
    : impl_(std::make_unique<Impl>(std::move(source))) {}

LldpObserver::~LldpObserver() = default;

bool LldpObserver::start() { return impl_->start(); }
void LldpObserver::stop() { impl_->stop(); }
bool LldpObserver::isRunning() const { return impl_->isRunning(); }

void LldpObserver::refreshAll() { impl_->refreshAll(); }
void LldpObserver::refreshInterface(const std::string& ifname) { impl_->refreshInterface(ifname); }

void LldpObserver::onInterfaceUp(const std::string& ifname) { impl_->onInterfaceUp(ifname); }
void LldpObserver::onInterfaceDown(const std::string& ifname) { impl_->onInterfaceDown(ifname); }
void LldpObserver::onInterfaceRemoved(const std::string& ifname) { impl_->onInterfaceRemoved(ifname); }

} // namespace RSCGroup