# SPDX-FileCopyrightText: 2021, Collabora, Ltd.
# SPDX-License-Identifier: CC0-1.0

with section("format"):
    line_width = 100
    tab_size = 8
    use_tabchars = True
    fractional_tab_policy = "use-space"

    max_prefix_chars = 4

    dangle_parens = True
    dangle_align = "prefix-indent"
    max_pargs_hwrap = 4
    max_rows_cmdline = 1

    keyword_case = 'upper'


# Do not reflow comments

with section("markup"):
    enable_markup = False
