/*
 * Copyright © 2026 Julia Klee
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __FLATPAK_WOLOS_CONSENT_PRIVATE_H__
#define __FLATPAK_WOLOS_CONSENT_PRIVATE_H__

#include "flatpak-dir-private.h"

/* Consent integration for WolOS. Everything here is a no-op unless
 * FLATPAK_WOLOS_CONSENT_HELPER (a program that may deny an install) or
 * FLATPAK_WOLOS_CONSENT_FILE (where grants are recorded) is set. */

gboolean   flatpak_wolos_consent_enabled (void);

/* Static permissions of @metakey as sorted "key=value" strings, e.g.
 * "filesystems=home", "sockets=x11", "session-bus:org.foo=talk".
 * @overrides (may be NULL) is merged on top. */
char     **flatpak_wolos_permissions (GKeyFile *metakey,
                                      GKeyFile *overrides);

/* Permissions in @new_perms that are not in @old_perms. */
char     **flatpak_wolos_permissions_added (const char * const *new_perms,
                                            const char * const *old_perms);

/* Runs the helper with the permissions @ref asks for (only those new
 * since @old_metakey on update). Fails with PERMISSION_DENIED when the
 * helper exits non-zero. TRUE when no helper is configured. */
gboolean   flatpak_wolos_consent_check (FlatpakDir        *dir,
                                        FlatpakDecomposed *ref,
                                        GKeyFile          *metakey,
                                        GKeyFile          *old_metakey,
                                        GError           **error);

/* Path of the consent file for @dir. */
char      *flatpak_wolos_consent_file (FlatpakDir *dir);

/* Rewrites @json (may be NULL) so @app holds exactly @perms (NULL
 * removes it). Other apps and top-level keys are kept. */
char      *flatpak_wolos_consent_set_grants (const char         *json,
                                             const char         *app,
                                             const char * const *perms);

/* Records the effective permissions (metadata + overrides) of the
 * deployed @ref, or forgets it when it is no longer installed. */
gboolean   flatpak_wolos_consent_sync (FlatpakDir        *dir,
                                       FlatpakDecomposed *ref,
                                       GError           **error);

#endif /* __FLATPAK_WOLOS_CONSENT_PRIVATE_H__ */
