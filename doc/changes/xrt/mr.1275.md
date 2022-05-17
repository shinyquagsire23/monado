---
- mr.1275
- mr.1279
- mr.1283
- mr.1285
- mr.1299
---

Introduce @ref xrt_system_devices struct and @ref xrt_instance_create_system
call. This is a prerequisite for setter uppers (system builders if will), also
have the added benefit of moving the logic of which devices has which role
solely into the service/instance.
