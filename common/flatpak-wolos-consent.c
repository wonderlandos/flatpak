/*
 * Copyright © 2026 Julia Klee
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <json-glib/json-glib.h>

#include "libglnx.h"

#include "flatpak-context-private.h"
#include "flatpak-error.h"
#include "flatpak-metadata-private.h"
#include "flatpak-utils-private.h"
#include "flatpak-wolos-consent-private.h"

#define CAPABILITY_PREFIX "flatpak:"

gboolean
flatpak_wolos_consent_enabled (void)
{
  const char *helper = g_getenv ("FLATPAK_WOLOS_CONSENT_HELPER");
  const char *file = g_getenv ("FLATPAK_WOLOS_CONSENT_FILE");

  return (helper != NULL && *helper != '\0') || (file != NULL && *file != '\0');
}

static void
add_group (GPtrArray  *out,
           GKeyFile   *keyfile,
           const char *group,
           const char *prefix,
           gboolean    split)
{
  g_auto(GStrv) keys = g_key_file_get_keys (keyfile, group, NULL, NULL);

  for (char **k = keys; k && *k; k++)
    {
      g_autofree char *value = g_key_file_get_value (keyfile, group, *k, NULL);
      g_auto(GStrv) values = NULL;

      if (value == NULL)
        continue;

      if (!split)
        {
          g_ptr_array_add (out, g_strdup_printf ("%s%s=%s", prefix, *k, value));
          continue;
        }

      values = g_strsplit (value, ";", -1);
      for (char **v = values; *v; v++)
        if (**v != '\0' && **v != '!')
          g_ptr_array_add (out, g_strdup_printf ("%s%s=%s", prefix, *k, *v));
    }
}

static int
cmp_strings (gconstpointer a, gconstpointer b)
{
  return g_strcmp0 (*(const char * const *) a, *(const char * const *) b);
}

char **
flatpak_wolos_permissions (GKeyFile *metakey,
                           GKeyFile *overrides)
{
  g_autoptr(FlatpakContext) context = flatpak_context_new ();
  g_autoptr(GKeyFile) flat = g_key_file_new ();
  g_autoptr(GPtrArray) out = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GPtrArray) unique = g_ptr_array_new ();

  if (metakey)
    flatpak_context_load_metadata (context, metakey, NULL);
  if (overrides)
    {
      g_autoptr(FlatpakContext) over = flatpak_context_new ();
      if (flatpak_context_load_metadata (over, overrides, NULL))
        flatpak_context_merge (context, over);
    }

  flatpak_context_save_metadata (context, TRUE, flat);

  add_group (out, flat, FLATPAK_METADATA_GROUP_CONTEXT, "", TRUE);
  add_group (out, flat, FLATPAK_METADATA_GROUP_SESSION_BUS_POLICY, "session-bus:", FALSE);
  add_group (out, flat, FLATPAK_METADATA_GROUP_SYSTEM_BUS_POLICY, "system-bus:", FALSE);

  g_ptr_array_sort (out, cmp_strings);
  for (guint i = 0; i < out->len; i++)
    if (unique->len == 0 ||
        g_strcmp0 (g_ptr_array_index (unique, unique->len - 1), g_ptr_array_index (out, i)) != 0)
      g_ptr_array_add (unique, g_strdup (g_ptr_array_index (out, i)));
  g_ptr_array_add (unique, NULL);

  return (char **) g_ptr_array_steal (unique, NULL);
}

char **
flatpak_wolos_permissions_added (const char * const *new_perms,
                                 const char * const *old_perms)
{
  g_autoptr(GPtrArray) out = g_ptr_array_new ();

  for (const char * const *p = new_perms; p && *p; p++)
    if (old_perms == NULL || !g_strv_contains (old_perms, *p))
      g_ptr_array_add (out, g_strdup (*p));
  g_ptr_array_add (out, NULL);

  return (char **) g_ptr_array_steal (out, NULL);
}

gboolean
flatpak_wolos_consent_check (FlatpakDir        *dir,
                             FlatpakDecomposed *ref,
                             GKeyFile          *metakey,
                             GKeyFile          *old_metakey,
                             GError           **error)
{
  const char *helper = g_getenv ("FLATPAK_WOLOS_CONSENT_HELPER");
  g_auto(GStrv) perms = NULL;
  g_auto(GStrv) asked = NULL;
  g_autoptr(GPtrArray) argv = NULL;
  g_autofree char *id = NULL;
  g_autoptr(GError) local_error = NULL;
  int status = 0;

  if (helper == NULL || *helper == '\0' || metakey == NULL ||
      !flatpak_decomposed_is_app (ref))
    return TRUE;

  perms = flatpak_wolos_permissions (metakey, NULL);
  if (old_metakey)
    {
      g_auto(GStrv) old_perms = flatpak_wolos_permissions (old_metakey, NULL);
      asked = flatpak_wolos_permissions_added ((const char * const *) perms,
                                               (const char * const *) old_perms);
      if (asked[0] == NULL)
        return TRUE; /* An update that asks for nothing new needs no sheet */
    }
  else
    asked = g_steal_pointer (&perms);

  id = flatpak_decomposed_dup_id (ref);
  argv = g_ptr_array_new ();
  g_ptr_array_add (argv, (char *) helper);
  g_ptr_array_add (argv, old_metakey ? "update" : "install");
  g_ptr_array_add (argv, id);
  g_ptr_array_add (argv, flatpak_dir_is_user (dir) ? "user" : "system");
  g_ptr_array_add (argv, "--");
  for (char **p = asked; *p; p++)
    g_ptr_array_add (argv, *p);
  g_ptr_array_add (argv, NULL);

  if (!g_spawn_sync (NULL, (char **) argv->pdata, NULL,
                     G_SPAWN_CHILD_INHERITS_STDIN, NULL, NULL,
                     NULL, NULL, &status, &local_error))
    return flatpak_fail_error (error, FLATPAK_ERROR_PERMISSION_DENIED,
                               _("Consent helper %s failed: %s"), helper, local_error->message);

  if (!g_spawn_check_wait_status (status, NULL))
    return flatpak_fail_error (error, FLATPAK_ERROR_PERMISSION_DENIED,
                               _("Permissions for %s were not granted"), id);

  return TRUE;
}

char *
flatpak_wolos_consent_file (FlatpakDir *dir)
{
  const char *file = g_getenv ("FLATPAK_WOLOS_CONSENT_FILE");

  if (file != NULL && *file != '\0')
    return g_strdup (file);
  if (flatpak_dir_is_user (dir))
    return g_build_filename (g_get_user_config_dir (), "wolos", "flatpak-consent.json", NULL);
  return g_strdup ("/etc/wolos/flatpak-consent.json");
}

char *
flatpak_wolos_consent_set_grants (const char         *json,
                                  const char         *app,
                                  const char * const *perms)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  g_autoptr(JsonNode) root = NULL;
  g_autoptr(JsonGenerator) gen = json_generator_new ();
  JsonObject *top = NULL;
  JsonArray *old_grants = NULL;
  g_autoptr(JsonArray) grants = json_array_new ();
  g_autofree char *out = NULL;

  /* Same rule as WolOS: a file that isn't the documented shape grants
   * nothing, so the edit starts it over. */
  if (json && json_parser_load_from_data (parser, json, -1, NULL) &&
      JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
    root = json_node_copy (json_parser_get_root (parser));
  else
    {
      root = json_node_new (JSON_NODE_OBJECT);
      json_node_take_object (root, json_object_new ());
    }
  top = json_node_get_object (root);

  if (json_object_has_member (top, "grants") &&
      JSON_NODE_HOLDS_ARRAY (json_object_get_member (top, "grants")))
    old_grants = json_object_get_array_member (top, "grants");

  for (guint i = 0; old_grants && i < json_array_get_length (old_grants); i++)
    {
      JsonNode *n = json_array_get_element (old_grants, i);
      JsonObject *g;

      if (!JSON_NODE_HOLDS_OBJECT (n))
        continue;
      g = json_node_get_object (n);
      if (g_strcmp0 (json_object_get_string_member_with_default (g, "app", NULL), app) == 0)
        continue;
      json_array_add_element (grants, json_node_copy (n));
    }

  for (const char * const *p = perms; p && *p; p++)
    {
      JsonObject *g = json_object_new ();
      g_autofree char *cap = g_strconcat (CAPABILITY_PREFIX, *p, NULL);
      json_object_set_string_member (g, "app", app);
      json_object_set_string_member (g, "capability", cap);
      json_array_add_object_element (grants, g);
    }

  json_object_set_array_member (top, "grants", g_steal_pointer (&grants));

  json_generator_set_pretty (gen, TRUE);
  json_generator_set_root (gen, root);
  out = json_generator_to_data (gen, NULL);
  return g_strconcat (out, "\n", NULL);
}

gboolean
flatpak_wolos_consent_sync (FlatpakDir        *dir,
                            FlatpakDecomposed *ref,
                            GError           **error)
{
  g_autofree char *file = NULL;
  g_autofree char *current = NULL;
  g_autofree char *updated = NULL;
  g_autofree char *id = NULL;
  g_autofree char *app = NULL;
  g_autofree char *dirname = NULL;
  g_autoptr(GFile) deploy = NULL;
  g_autoptr(GKeyFile) metakey = NULL;
  g_autoptr(GKeyFile) overrides = NULL;
  g_auto(GStrv) perms = NULL;

  if (!flatpak_wolos_consent_enabled () || !flatpak_decomposed_is_app (ref))
    return TRUE;

  id = flatpak_decomposed_dup_id (ref);
  app = g_build_filename (flatpak_file_get_path_cached (flatpak_dir_get_path (dir)), "app", id, NULL);

  deploy = flatpak_dir_get_if_deployed (dir, ref, NULL, NULL);
  if (deploy)
    {
      g_autoptr(GFile) meta = g_file_get_child (deploy, "metadata");
      metakey = g_key_file_new ();
      if (!g_key_file_load_from_file (metakey, flatpak_file_get_path_cached (meta), 0, NULL))
        g_clear_pointer (&metakey, g_key_file_unref);
      overrides = flatpak_load_override_keyfile (id, flatpak_dir_is_user (dir), NULL);
      if (metakey)
        perms = flatpak_wolos_permissions (metakey, overrides);
    }

  file = flatpak_wolos_consent_file (dir);
  (void) g_file_get_contents (file, &current, NULL, NULL);
  updated = flatpak_wolos_consent_set_grants (current, app, (const char * const *) perms);
  if (g_strcmp0 (current, updated) == 0)
    return TRUE;

  dirname = g_path_get_dirname (file);
  if (g_mkdir_with_parents (dirname, 0755) != 0)
    return glnx_throw_errno_prefix (error, "mkdir %s", dirname);

  return g_file_set_contents (file, updated, -1, error);
}
