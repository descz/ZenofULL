#include <zeno.h>

int main(void) {
    ZenoConfig config;
    zeno_config_default(&config);
    return config.max_agent_turns > 0 ? 0 : 1;
}
