# cycfx2prog - EZ-USB FX2 programmer and generic USB protocol tester

This is an enhanced version of cycfx2prog, developed and distributed by Wolfgang Wieser at https://www.triplespark.net/elec/periph/USB-FX2/software/.

Contrary to its FX2-only sounding name, this tool can be used as a generic USB protocol tester, sending many types of USB requests. The only enhancedment I have done is to add commands to test USB INTERRUPT transfer (dint, sint, fint, bench_int), which were actually all implemented internally, but lacked a CLI interface.
