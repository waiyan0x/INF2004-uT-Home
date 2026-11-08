// ESPHome requires a C++ component boundary. The shared decision code is C99.
#pragma once
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c_bus.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "txwire.h"
#include <string>

namespace esphome::runtime_rules
{
class RuleBridge : public Component
{
public:
    void set_bus(i2c::I2CBus * p_bus) { p_bus_ = p_bus; }
    void set_topic_prefix(const std::string & prefix)
    {
        request_topic_ = prefix + "/request";
        reply_topic_ = prefix + "/reply";
    }
    void setup() override;
    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

private:
    static txrule_status_t read(void * p_context,
                               const txrule_config_t * p_rule,
                               uint8_t * p_data);
    i2c::I2CBus * p_bus_{nullptr};
    txrule_t state_{};
    std::string request_topic_;
    std::string reply_topic_;
};
} // namespace esphome::runtime_rules
