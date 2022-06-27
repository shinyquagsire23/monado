# SPDX-License-Identifier: CC0-1.0
# SPDX-FileCopyrightText: 2022 Collabora, Ltd. and the Monado contributors
#
# To generate all the templated files, run this from the root of the repo:
#   make -f .gitlab-ci/ci-scripts.mk

# These also all have their template named the same with a .jinja suffix.
FILES_IN_SUBDIR := \
    .gitlab-ci/distributions \
    .gitlab-ci/reprepro.sh \
	.gitlab-ci/install-android-sdk.sh \

CONFIG_FILE := .gitlab-ci/config.yml
OUTPUTS := .gitlab-ci.yml \
    $(FILES_IN_SUBDIR)

all: $(OUTPUTS)
	chmod +x .gitlab-ci/*.sh
.PHONY: all

clean:
	rm -f $(OUTPUTS)
.PHONY: clean

CI_FAIRY := ci-fairy generate-template --config=$(CONFIG_FILE)

# As the default thing for ci-fairy to template, this is special cased
.gitlab-ci.yml: .gitlab-ci/ci.template .gitlab-ci/win_containers.yml $(CONFIG_FILE)
	$(CI_FAIRY) $< > $@

# Everything else is structured alike
$(FILES_IN_SUBDIR): %: %.jinja $(CONFIG_FILE)
	$(CI_FAIRY) $< > $@
