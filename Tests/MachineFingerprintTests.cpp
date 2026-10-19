#include "Licensing/MachineFingerprint.h"
#include <iostream>
int main() {
    const auto first = licensing::MachineFingerprint::createMachineIdHash();
    if (first.length() != 64 || ! first.containsOnly("0123456789abcdef")) return 1;
    for (int i = 0; i < 100; ++i)
        if (licensing::MachineFingerprint::createMachineIdHash() != first) return 2;
    std::cout << "Stable machine ID: valid SHA-256 and repeatable across 100 reads\n";
    return 0;
}
