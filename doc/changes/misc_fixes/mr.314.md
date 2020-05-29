windows: Way way back when Gallium was made `auxiliary` was named `aux` but then
it was ported to Windows and it was renamed to `auxiliary` since Windows is
[allergic to filenames that match its device names](https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file#naming-conventions)
(e.g., `AUX`, `CON`, `PRN`, etc.). Through the ages, this knowledge was lost and
so we find ourselves with the same problem. Although Monado inherited the
correct name, the same old mistake was made in docs.
