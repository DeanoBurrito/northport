# X86_64 Implementation details

## Required Hardware Features
- CPUID
- Local APIC
- TSC
- HPET or PIT (one must be present)
- Fxsave instruction

## Optional Hardware Features
- IO APICs
- TSC deadline
- la57 support
- SMAP + SMEP
- UMIP
- Global pages
- 1G pages
- NX-bit support
- X2 APIC mode
- XSave
- PAT
- Invlpgb (broadcast tlb flush)
