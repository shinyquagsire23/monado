---
- mr.408
- mr.409
---
Centralise the logging functionality in Monado to a single util helper.
Previously most of our logging was done via fprints and gated behind booleans,
now there are common functions to call and a predfined set of levels.
