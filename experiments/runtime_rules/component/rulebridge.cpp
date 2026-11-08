#include "rulebridge.hpp"

namespace esphome::runtime_rules
{
void RuleBridge::setup()
{
    txrule_init(&state_);
    mqtt::global_mqtt_client->subscribe(
        request_topic_,
        [this](const std::string &, const std::string & payload)
        {
            uint8_t reply[TXWIRE_REPLY_SIZE] = {};
            // ESPHome dispatches MQTT callbacks from its main loop. That loop
            // is the single owner of rule loading and synchronous execution.
            txwire_process(&state_,
                reinterpret_cast<const uint8_t *>(payload.data()),
                payload.size(), read, this, reply);
            const bool published = mqtt::global_mqtt_client->publish(
                reply_topic_, reinterpret_cast<const char *>(reply),
                sizeof(reply), 0, false);
            if (!published)
            {
                this->status_set_warning();
            }
            else
            {
                this->status_clear_warning();
            }
        }, 0);
}

txrule_status_t RuleBridge::read(void * p_context,
                               const txrule_config_t * p_rule,
                               uint8_t * p_data)
{
    auto * p_self = static_cast<RuleBridge *>(p_context);
    if (nullptr == p_self->p_bus_)
    {
        return TXRULE_IO;
    }
    const uint8_t command = static_cast<uint8_t>(p_rule->reg);
    const auto status = p_self->p_bus_->write_readv(
        static_cast<uint8_t>(p_rule->address), &command, 1,
        p_data, p_rule->read_size);
    if (i2c::NO_ERROR == status)
    {
        return TXRULE_OK;
    }
    return (i2c::ERROR_TIMEOUT == status) ? TXRULE_TIMEOUT : TXRULE_IO;
}
} // namespace esphome::runtime_rules
