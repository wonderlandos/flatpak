#!/bin/bash
#
# Copyright © 2026 Julia Klee
# SPDX-License-Identifier: LGPL-2.1-or-later

set -euo pipefail

. $(dirname $0)/libtest.sh

skip_revokefs_without_fuse

echo "1..5"

export BUILD_FINISH_ARGS="--socket=x11 --filesystem=home --talk-name=org.example.Foo"
setup_repo

helper=${test_tmpdir}/consent-helper
cat > $helper <<'HELPER'
#!/bin/sh
printf '%s\n' "$@" > "$CONSENT_ARGS"
exit "${CONSENT_EXIT:-0}"
HELPER
chmod +x $helper
export CONSENT_ARGS=${test_tmpdir}/consent-args
consent=${test_tmpdir}/consent.json

# Unconfigured: nothing asked, nothing recorded
${FLATPAK} ${U} install -y test-repo org.test.Hello >&2
assert_not_has_file $CONSENT_ARGS
assert_not_has_file $XDG_CONFIG_HOME/wolos/flatpak-consent.json
${FLATPAK} ${U} uninstall -y org.test.Hello >&2

ok "no helper, upstream behaviour"

if CONSENT_EXIT=1 ${FLATPAK} ${U} install -y --wolos-consent=$helper test-repo org.test.Hello >&2 2> install-err; then
    assert_not_reached "install should be refused"
fi
assert_file_has_content $CONSENT_ARGS "^install$"
assert_file_has_content $CONSENT_ARGS "^org\.test\.Hello$"
assert_file_has_content $CONSENT_ARGS "^sockets=x11$"
assert_file_has_content $CONSENT_ARGS "^filesystems=home$"
assert_file_has_content $CONSENT_ARGS "^session-bus:org\.example\.Foo=talk$"
assert_not_has_dir $FL_DIR/app/org.test.Hello/$ARCH/master/active

ok "helper can refuse"

FLATPAK_WOLOS_CONSENT_FILE=$consent ${FLATPAK} ${U} install -y --wolos-consent=$helper test-repo org.test.Hello >&2
assert_has_dir $FL_DIR/app/org.test.Hello/$ARCH/master/active
assert_file_has_content $consent '"app" : ".*/app/org\.test\.Hello"'
assert_file_has_content $consent '"capability" : "flatpak:sockets=x11"'
assert_file_has_content $consent '"capability" : "flatpak:filesystems=home"'

ok "grants recorded"

FLATPAK_WOLOS_CONSENT_FILE=$consent ${FLATPAK} override ${U} --nofilesystem=home --socket=wayland org.test.Hello
assert_not_file_has_content $consent 'flatpak:filesystems=home'
assert_file_has_content $consent 'flatpak:sockets=wayland'
FLATPAK_WOLOS_CONSENT_FILE=$consent ${FLATPAK} override ${U} --reset org.test.Hello
assert_file_has_content $consent 'flatpak:filesystems=home'

ok "overrides update grants"

FLATPAK_WOLOS_CONSENT_FILE=$consent ${FLATPAK} ${U} uninstall -y org.test.Hello >&2
assert_not_file_has_content $consent 'org\.test\.Hello'
assert_file_has_content $consent '"grants"'

ok "uninstall forgets grants"
