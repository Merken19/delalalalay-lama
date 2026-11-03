/* Smoke test for the platform bridge this test is linked with: the Windows
 * bridge in CTest, or the CoreMIDI bridge in Tests/run_macos_tests.sh. */

#include "bridge_checks.h"

int main(void) {
    DelayLamaHandle *handle = dl_create(48000.0f);
    double energy = 0.0;

    if (!handle) {
        fprintf(stderr, "bridge: dl_create failed\n");
        return 1;
    }
    if (run_bridge_checks(handle, &energy) != 0) {
        dl_destroy(handle);
        return 1;
    }

    printf("bridge_smoke: PASS energy=%.6f midi_sources=%d\n", energy,
           (int)dl_midi_source_count(handle));
    dl_destroy(handle);
    return 0;
}
