// cloudflare.h - purge Cloudflare's edge cache for a static site's published
// output. Include after nob.h, having already #define'd:
//   SITE_URL       - e.g. "https://guycho.uk" (no trailing slash)
//   OUT_DIR        - the built output directory, e.g. "./public"
//   CF_PASS_PREFIX - `pass` entry prefix holding "<prefix>/zone-id" and
//                    "<prefix>/api-token". A site that isn't its own zone
//                    (e.g. blog.guycho.uk, which lives under the guycho.uk
//                    zone) points this at the parent zone's prefix instead
//                    of its own hostname.

#ifndef CLOUDFLARE_H_
#define CLOUDFLARE_H_

#define CF_PURGE_BATCH 30 // Cloudflare's max files per purge_cache call

// Runs `pass <entry>` and returns its output, trimmed. Owned, never freed.
static char *cf_read_pass(const char *entry)
{
    const char *tmp = nob_temp_sprintf("/tmp/nob-pass-%d.txt", getpid());
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "pass", entry);
    if (!nob_cmd_run(&cmd, .stdout_path = tmp)) {
        nob_log(NOB_ERROR, "pass %s: failed - is it unlocked?", entry);
        return NULL;
    }
    Nob_String_Builder out = {0};
    if (!nob_read_entire_file(tmp, &out)) return NULL;
    remove(tmp);
    while (out.count > 0 && (out.items[out.count - 1] == '\n' || out.items[out.count - 1] == '\r')) {
        out.count--;
    }
    nob_sb_append_null(&out);
    return out.items;
}

static bool cf_collect_public_url(Nob_Walk_Entry entry)
{
    if (entry.type != NOB_FILE_REGULAR) return true;
    Nob_File_Paths *urls = entry.data;
    // entry.path is "<OUT_DIR>/<rel>"; SITE_URL + "/<rel>" is the live URL.
    const char *rel = entry.path + strlen(OUT_DIR);
    nob_da_append(urls, strdup(nob_temp_sprintf(SITE_URL "%s", rel)));
    return true;
}

// Purges every file actually published under OUT_DIR (plus the bare "/")
// from Cloudflare's edge cache for this site specifically - not
// purge_everything, which would also nuke every sibling subdomain sharing
// the same zone. Credentials come from CF_PASS_PREFIX in `pass`, never
// hardcoded or committed.
static bool purge_cache(void)
{
    char *zone_id = cf_read_pass(CF_PASS_PREFIX "/zone-id");
    char *token = cf_read_pass(CF_PASS_PREFIX "/api-token");
    if (!zone_id || !token) return false;

    Nob_File_Paths urls = {0};
    nob_da_append(&urls, strdup(SITE_URL "/"));
    if (!nob_walk_dir(OUT_DIR, cf_collect_public_url, .data = &urls)) return false;

    const char *endpoint = nob_temp_sprintf(
        "https://api.cloudflare.com/client/v4/zones/%s/purge_cache", zone_id);
    const char *auth = nob_temp_sprintf("Authorization: Bearer %s", token);

    for (size_t i = 0; i < urls.count; i += CF_PURGE_BATCH) {
        size_t end = i + CF_PURGE_BATCH < urls.count ? i + CF_PURGE_BATCH : urls.count;
        Nob_String_Builder body = {0};
        nob_sb_append_cstr(&body, "{\"files\":[");
        for (size_t j = i; j < end; j++) {
            if (j > i) nob_sb_append_cstr(&body, ",");
            nob_sb_appendf(&body, "\"%s\"", urls.items[j]);
        }
        nob_sb_append_cstr(&body, "]}");
        nob_sb_append_null(&body);

        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "curl", "-s", "-X", "POST", endpoint,
                       "-H", auth, "-H", "Content-Type: application/json",
                       "--data", body.items);
        nob_log(NOB_INFO, "purging %zu url(s)...", end - i);
        // The rendered command line carries the bearer token in plaintext;
        // don't let nob's default command-echo put it in the terminal/logs.
        Nob_Log_Level prev = nob_minimal_log_level;
        nob_minimal_log_level = NOB_WARNING;
        bool ok = nob_cmd_run_sync_and_reset(&cmd);
        nob_minimal_log_level = prev;
        if (!ok) return false;
        nob_sb_free(body);
    }

    nob_log(NOB_INFO, "purged %zu url(s) total", urls.count);
    return true;
}

#endif // CLOUDFLARE_H_
