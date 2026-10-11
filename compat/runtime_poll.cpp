#include <cstdint>
extern "C" void yield_self_1ms(uint8_t* rdram);
// The native loop waits for an interrupt-driven scheduler to clear its flag.
// Preserve the guest condition and registers while servicing host event delivery.
extern "C" void rush_poll_runtime(uint8_t* rdram) {
    yield_self_1ms(rdram);
}
