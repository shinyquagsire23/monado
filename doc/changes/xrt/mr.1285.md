---
- mr.1285
- mr.1299
- mr.1313
---

Introduce @ref xrt_builder struct and various functions on @ref xrt_prober
to support them. This is a follow up on the @ref xrt_system_devices changes.
These make it much easier to build more complex all in one devices like WinMR
and Rift-S. And moves a lot of contextual configuration out of generic drivers
like the hand-tracker code needing to know which device it was being used by.
