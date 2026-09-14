#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "zeno_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* =====================================================================
 * Zeno Studio backend — pure C11.
 * Voice (Deepgram-first, OpenAI optional), settings, projects, remotes,
 * plugins. File-backed with the Markdown+JSON envelope used by memory.
 * No network here: helpers build provider URLs and parse provider JSON;
 * the browser/native shell performs audio fetch with the user key.
 * Voice contract: entering voice mode must NOT open a textual chat;
 * the agent greeting is spoken automatically (auto_speak = 1).
 * ===================================================================== */

static unsigned long studio_seq = 0;

static int studio_streq_ci(const char *a, const char *b) {
    if (a == NULL || b == NULL) return 0;
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void studio_copy_trunc(char *dst, size_t size, const char *src, const char *fallback) {
    if (dst == NULL || size == 0) return;
    const char *text = (src != NULL && *src != '\0') ? src : fallback;
    if (text == NULL) text = "";
    zeno_copy_string(dst, size, text);
}

static char *studio_store_read(const char *path, const char *fallback_json) {
    if (path == NULL || *path == '\0') return zeno_strdup(fallback_json != NULL ? fallback_json : "{}");
    return zeno_markdown_read_json(path, fallback_json != NULL ? fallback_json : "{}");
}

static int studio_store_write(const char *path, const char *title, const char *json) {
    if (path == NULL || *path == '\0' || json == NULL) return 0;
    return zeno_markdown_write_json(path, title != NULL ? title : "Zeno Data", json);
}

static void studio_slugify(const char *text, char *out, size_t out_size) {
    size_t write = 0;
    int last_dash = 1;
    if (out == NULL || out_size == 0) return;
    out[0] = '\0';
    if (text == NULL) text = "";
    for (size_t i = 0; text[i] != '\0' && write + 1 < out_size; i++) {
        unsigned char c = (unsigned char)text[i];
        if (isalnum(c)) {
            out[write++] = (char)tolower(c);
            last_dash = 0;
        } else if (!last_dash && write > 0) {
            out[write++] = '-';
            last_dash = 1;
        }
    }
    while (write > 0 && out[write - 1] == '-') write--;
    out[write] = '\0';
    if (write < 2) zeno_copy_string(out, out_size, "plugin");
    if (strlen(out) > 48) out[48] = '\0';
}

static char *studio_make_id(const char *prefix) {
    studio_seq++;
    return zeno_format("%s_%lld_%lu", prefix != NULL ? prefix : "id",
                       zeno_now_ms(), studio_seq);
}

static int studio_valid_id(const char *id) {
    size_t len;
    if (id == NULL) return 0;
    len = strlen(id);
    if (len < 2 || len > 64) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) return 0;
    }
    return 1;
}

/* ---------------- voice ---------------- */

void zeno_voice_config_default(ZenoVoiceConfig *config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    zeno_copy_string(config->provider, sizeof(config->provider), "deepgram");
    zeno_copy_string(config->voice, sizeof(config->voice), "aura-asteria-en");
    zeno_copy_string(config->language, sizeof(config->language), "pt-BR");
    zeno_copy_string(config->stt_model, sizeof(config->stt_model), "nova-3");
    zeno_copy_string(config->tts_model, sizeof(config->tts_model), "aura-asteria-en");
    config->auto_speak = 1;
}

static void studio_normalize_provider(const char *raw, char *out, size_t size) {
    if (studio_streq_ci(raw, "openai") || studio_streq_ci(raw, "gpt") ||
        studio_streq_ci(raw, "whisper") || studio_streq_ci(raw, "tts")) {
        zeno_copy_string(out, size, "openai");
        return;
    }
    zeno_copy_string(out, size, "deepgram");
}

static void studio_normalize_lang(const char *raw, char *out, size_t size) {
    if (raw == NULL || *raw == '\0') {
        zeno_copy_string(out, size, "pt-BR");
        return;
    }
    if (studio_streq_ci(raw, "en") || studio_streq_ci(raw, "en-us") ||
        studio_streq_ci(raw, "en_US") || studio_streq_ci(raw, "english")) {
        zeno_copy_string(out, size, "en-US");
        return;
    }
    if (studio_streq_ci(raw, "es") || studio_streq_ci(raw, "es-es") ||
        studio_streq_ci(raw, "es_ES") || studio_streq_ci(raw, "spanish") ||
        studio_streq_ci(raw, "espanol") || studio_streq_ci(raw, "español")) {
        zeno_copy_string(out, size, "es-ES");
        return;
    }
    zeno_copy_string(out, size, "pt-BR");
}

int zeno_voice_config_load_env(ZenoVoiceConfig *config) {
    const char *value;
    if (config == NULL) return 0;
    zeno_voice_config_default(config);
    value = getenv("ZENO_VOICE_PROVIDER");
    if (value == NULL) value = getenv("VOICE_PROVIDER");
    if (value != NULL && *value != '\0') studio_normalize_provider(value, config->provider, sizeof(config->provider));
    value = getenv("ZENO_VOICE_VOICE");
    if (value == NULL) value = getenv("DEEPGRAM_VOICE");
    if (value == NULL) value = getenv("OPENAI_VOICE");
    if (value != NULL && *value != '\0') studio_copy_trunc(config->voice, sizeof(config->voice), value, config->voice);
    value = getenv("ZENO_VOICE_LANG");
    if (value == NULL) value = getenv("DEEPGRAM_LANGUAGE");
    if (value == NULL) value = getenv("VOICE_LANG");
    if (value != NULL && *value != '\0') studio_normalize_lang(value, config->language, sizeof(config->language));
    value = getenv("ZENO_VOICE_STT_MODEL");
    if (value == NULL) value = getenv("DEEPGRAM_MODEL");
    if (value == NULL) value = getenv("STT_MODEL");
    if (value != NULL && *value != '\0') studio_copy_trunc(config->stt_model, sizeof(config->stt_model), value, config->stt_model);
    value = getenv("ZENO_VOICE_TTS_MODEL");
    if (value == NULL) value = getenv("TTS_MODEL");
    if (value != NULL && *value != '\0') studio_copy_trunc(config->tts_model, sizeof(config->tts_model), value, config->tts_model);
    if (strcmp(config->provider, "openai") == 0) {
        if (studio_streq_ci(config->stt_model, "nova-3") || studio_streq_ci(config->stt_model, "nova-2"))
            zeno_copy_string(config->stt_model, sizeof(config->stt_model), "whisper-1");
        if (studio_streq_ci(config->tts_model, "aura-asteria-en"))
            zeno_copy_string(config->tts_model, sizeof(config->tts_model), "tts-1");
        if (studio_streq_ci(config->voice, "aura-asteria-en"))
            zeno_copy_string(config->voice, sizeof(config->voice), "alloy");
    } else {
        if (studio_streq_ci(config->stt_model, "whisper-1"))
            zeno_copy_string(config->stt_model, sizeof(config->stt_model), "nova-3");
        if (studio_streq_ci(config->tts_model, "tts-1"))
            zeno_copy_string(config->tts_model, sizeof(config->tts_model), "aura-asteria-en");
        if (studio_streq_ci(config->voice, "alloy"))
            zeno_copy_string(config->voice, sizeof(config->voice), "aura-asteria-en");
    }
    value = getenv("ZENO_VOICE_AUTO_SPEAK");
    if (value != NULL && *value != '\0') {
        if (studio_streq_ci(value, "0") || studio_streq_ci(value, "false") ||
            studio_streq_ci(value, "no") || studio_streq_ci(value, "off"))
            config->auto_speak = 0;
        else
            config->auto_speak = 1;
    }
    return 1;
}

char *zeno_voice_config_json(const ZenoVoiceConfig *config) {
    ZenoVoiceConfig fallback;
    const ZenoVoiceConfig *c = config;
    char *provider;
    char *voice;
    char *language;
    char *stt;
    char *tts;
    char *result;
    if (c == NULL) {
        zeno_voice_config_default(&fallback);
        c = &fallback;
    }
    provider = zeno_json_escape(c->provider);
    voice = zeno_json_escape(c->voice);
    language = zeno_json_escape(c->language);
    stt = zeno_json_escape(c->stt_model);
    tts = zeno_json_escape(c->tts_model);
    result = zeno_format("{\"provider\":%s,\"voice\":%s,\"language\":%s,"
                         "\"stt_model\":%s,\"tts_model\":%s,\"auto_speak\":%s}",
                         provider != NULL ? provider : "\"deepgram\"",
                         voice != NULL ? voice : "\"aura-asteria-en\"",
                         language != NULL ? language : "\"pt-BR\"",
                         stt != NULL ? stt : "\"nova-3\"",
                         tts != NULL ? tts : "\"aura-asteria-en\"",
                         c->auto_speak ? "true" : "false");
    free(provider);
    free(voice);
    free(language);
    free(stt);
    free(tts);
    return result;
}

int zeno_voice_config_save(const char *store_path, const ZenoVoiceConfig *config) {
    char *json;
    int ok;
    if (store_path == NULL || config == NULL) return 0;
    json = zeno_voice_config_json(config);
    if (json == NULL) return 0;
    ok = studio_store_write(store_path, "Zeno Voice Config", json);
    free(json);
    return ok;
}

int zeno_voice_config_load(const char *store_path, ZenoVoiceConfig *config) {
    char *json;
    char *error = NULL;
    ZjNode *root;
    const char *value;
    if (config == NULL) return 0;
    zeno_voice_config_default(config);
    if (store_path == NULL || *store_path == '\0') return 1;
    json = studio_store_read(store_path, "{}");
    if (json == NULL) return 1;
    root = zj_parse(json, &error);
    free(error);
    free(json);
    if (root == NULL) return 1;
    value = zj_string(zj_object_get(root, "provider"));
    if (value != NULL) studio_normalize_provider(value, config->provider, sizeof(config->provider));
    value = zj_string(zj_object_get(root, "voice"));
    if (value != NULL && *value != '\0') studio_copy_trunc(config->voice, sizeof(config->voice), value, config->voice);
    value = zj_string(zj_object_get(root, "language"));
    if (value != NULL && *value != '\0') studio_normalize_lang(value, config->language, sizeof(config->language));
    value = zj_string(zj_object_get(root, "stt_model"));
    if (value != NULL && *value != '\0') studio_copy_trunc(config->stt_model, sizeof(config->stt_model), value, config->stt_model);
    value = zj_string(zj_object_get(root, "tts_model"));
    if (value != NULL && *value != '\0') studio_copy_trunc(config->tts_model, sizeof(config->tts_model), value, config->tts_model);
    {
        ZjNode *auto_node = zj_object_get(root, "auto_speak");
        if (auto_node != NULL) config->auto_speak = zj_bool(auto_node, config->auto_speak) ? 1 : 0;
    }
    zj_free(root);
    return 1;
}

char *zeno_voice_greeting(const ZenoVoiceConfig *config) {
    char lang[16];
    if (config != NULL) studio_normalize_lang(config->language, lang, sizeof(lang));
    else zeno_copy_string(lang, sizeof(lang), "pt-BR");
    if (strcmp(lang, "en-US") == 0)
        return zeno_strdup("Hi! I'm Zeno. Talk to me, I'm listening.");
    if (strcmp(lang, "es-ES") == 0)
        return zeno_strdup("Hola! Soy Zeno. Habla conmigo, te escucho.");
    return zeno_strdup("Ol\xc3\xa1! Sou o Zeno. Fale comigo, estou ouvindo.");
}

char *zeno_voice_stt_url(const ZenoVoiceConfig *config, const char *lang_override) {
    ZenoVoiceConfig fallback;
    const ZenoVoiceConfig *c = config;
    char lang[16];
    if (c == NULL) {
        zeno_voice_config_default(&fallback);
        c = &fallback;
    }
    if (lang_override != NULL && *lang_override != '\0') studio_normalize_lang(lang_override, lang, sizeof(lang));
    else studio_normalize_lang(c->language, lang, sizeof(lang));
    if (strcmp(c->provider, "openai") == 0)
        return zeno_strdup("https://api.openai.com/v1/audio/transcriptions");
    /* Query values are provider-controlled slugs ([A-Za-z0-9._-]); no
     * percent-encoding needed for the supported model/language set. */
    return zeno_format("https://api.deepgram.com/v1/listen?model=%s&language=%s"
                       "&punctuate=true&smart_format=true",
                       c->stt_model[0] != '\0' ? c->stt_model : "nova-3", lang);
}

char *zeno_voice_tts_url(const ZenoVoiceConfig *config, const char *voice_override) {
    ZenoVoiceConfig fallback;
    const ZenoVoiceConfig *c = config;
    if (c == NULL) {
        zeno_voice_config_default(&fallback);
        c = &fallback;
    }
    if (strcmp(c->provider, "openai") == 0)
        return zeno_strdup("https://api.openai.com/v1/audio/speech");
    if (voice_override != NULL && *voice_override != '\0' && strlen(voice_override) <= 64)
        return zeno_format("https://api.deepgram.com/v1/speak?model=%s", voice_override);
    return zeno_format("https://api.deepgram.com/v1/speak?model=%s",
                       (c->tts_model[0] != '\0') ? c->tts_model : "aura-asteria-en");
}

char *zeno_voice_transcript_extract(const char *provider_json) {
    char *error = NULL;
    ZjNode *root;
    const char *text = NULL;
    char *result = NULL;
    if (provider_json == NULL || *provider_json == '\0') return NULL;
    root = zj_parse(provider_json, &error);
    free(error);
    if (root == NULL) return NULL;
    /* Deepgram: results.channels[0].alternatives[0].transcript */
    {
        ZjNode *results = zj_object_get(root, "results");
        ZjNode *channels = results != NULL ? zj_object_get(results, "channels") : NULL;
        ZjNode *first = channels != NULL ? zj_array_get(channels, 0) : NULL;
        ZjNode *alts = first != NULL ? zj_object_get(first, "alternatives") : NULL;
        ZjNode *alt0 = alts != NULL ? zj_array_get(alts, 0) : NULL;
        text = alt0 != NULL ? zj_string(zj_object_get(alt0, "transcript")) : NULL;
    }
    if (text == NULL || *text == '\0') {
        /* OpenAI whisper verbose_json / plain: {"text": "..."} */
        text = zj_string(zj_object_get(root, "text"));
    }
    if (text != NULL) {
        char *trimmed = zeno_trim_copy(text);
        if (trimmed != NULL && *trimmed != '\0') result = trimmed;
        else free(trimmed);
    }
    zj_free(root);
    return result;
}

char *zeno_voice_describe(const ZenoVoiceConfig *config) {
    ZenoVoiceConfig fallback;
    const ZenoVoiceConfig *c = config;
    char *stt;
    char *tts;
    char *greeting;
    char *cfg;
    char *result;
    if (c == NULL) {
        zeno_voice_config_default(&fallback);
        c = &fallback;
    }
    stt = zeno_voice_stt_url(c, NULL);
    tts = zeno_voice_tts_url(c, NULL);
    greeting = zeno_voice_greeting(c);
    cfg = zeno_voice_config_json(c);
    {
        char *stt_esc = zeno_json_escape(stt != NULL ? stt : "");
        char *tts_esc = zeno_json_escape(tts != NULL ? tts : "");
        char *greet_esc = zeno_json_escape(greeting != NULL ? greeting : "");
        result = zeno_format("{\"config\":%s,\"stt_url\":%s,\"tts_url\":%s,"
                             "\"greeting\":%s,\"note\":%s}",
                             cfg != NULL ? cfg : "{}",
                             stt_esc != NULL ? stt_esc : "\"\"",
                             tts_esc != NULL ? tts_esc : "\"\"",
                             greet_esc != NULL ? greet_esc : "\"\"",
                             "\"entering voice must not open a textual chat; speak greeting automatically\"");
        free(stt_esc);
        free(tts_esc);
        free(greet_esc);
    }
    free(stt);
    free(tts);
    free(greeting);
    free(cfg);
    return result;
}

/* ---------------- settings ---------------- */

char *zeno_settings_defaults_json(void) {
    return zeno_strdup("{\"appearance\":{\"color_mode\":\"dark\",\"light_theme\":"
                       "\"openchamber-light\",\"dark_theme\":\"openchamber-dark\","
                       "\"language\":\"en\",\"time_format\":\"auto\"},"
                       "\"chat\":{\"font_size\":\"medium\",\"enter_behavior\":\"send\","
                       "\"show_suggestions\":true,\"compact_mode\":false},"
                       "\"notifications\":{\"enabled\":true,\"sound\":true,"
                       "\"mention_only\":false,\"desktop\":true},"
                       "\"shortcuts\":{\"new_chat\":\"Ctrl+N\",\"palette\":\"Ctrl+K\","
                       "\"settings\":\"Ctrl+,\",\"voice\":\"Ctrl+Shift+V\"},"
                       "\"voice\":{\"provider\":\"deepgram\",\"voice\":\"aura-asteria-en\","
                       "\"language\":\"pt-BR\",\"stt_model\":\"nova-3\","
                       "\"tts_model\":\"aura-asteria-en\",\"auto_speak\":true}}");
}

static int studio_looks_like_json(const char *value) {
    if (value == NULL) return 0;
    while (isspace((unsigned char)*value)) value++;
    if (*value == '\0') return 0;
    if (*value == '{' || *value == '[' || *value == '"') return 1;
    if (strncmp(value, "true", 4) == 0 || strncmp(value, "false", 5) == 0 ||
        strncmp(value, "null", 4) == 0) return 1;
    if (*value == '-' || isdigit((unsigned char)*value)) return 1;
    return 0;
}

char *zeno_settings_list(const char *store_path) {
    char *defaults = zeno_settings_defaults_json();
    char *json;
    if (store_path == NULL || *store_path == '\0') return defaults;
    json = studio_store_read(store_path, defaults != NULL ? defaults : "{}");
    if (json == NULL) return defaults;
    {
        char *error = NULL;
        ZjNode *holder = zj_parse(json, &error);
        int has_voice = 0;
        free(error);
        if (holder != NULL && holder->type == ZJ_OBJECT)
            has_voice = zj_object_get(holder, "voice") != NULL;
        zj_free(holder);
        if (!has_voice) {
            free(json);
            if (defaults != NULL) {
                (void)studio_store_write(store_path, "Zeno Settings", defaults);
                return defaults;
            }
            return zeno_settings_defaults_json();
        }
    }
    free(defaults);
    return json;
}

char *zeno_settings_get(const char *store_path, const char *section, const char *key) {
    char *json;
    char *error = NULL;
    ZjNode *root;
    ZjNode *sec;
    ZjNode *node;
    char *result = NULL;
    if (section == NULL || key == NULL) return NULL;
    json = zeno_settings_list(store_path);
    if (json == NULL) return NULL;
    root = zj_parse(json, &error);
    free(error);
    free(json);
    if (root == NULL) return NULL;
    sec = zj_object_get(root, section);
    node = sec != NULL ? zj_object_get(sec, key) : NULL;
    if (node != NULL) result = zj_stringify_compact(node);
    zj_free(root);
    return result;
}

int zeno_settings_set(const char *store_path, const char *section, const char *key,
                      const char *value_text) {
    char *json;
    char *error = NULL;
    ZjNode *root;
    char *value_json = NULL;
    char *result = NULL;
    int ok = 0;
    static const char *known[] = {"appearance", "chat", "notifications", "shortcuts", "voice"};
    if (store_path == NULL || section == NULL || key == NULL || value_text == NULL) return 0;
    if (*section == '\0' || *key == '\0' || strlen(section) > 64 || strlen(key) > 64) return 0;
    for (const char *p = section; *p != '\0'; p++)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return 0;
    for (const char *p = key; *p != '\0'; p++)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return 0;
    if (studio_looks_like_json(value_text)) {
        ZjNode *probe = zj_parse(value_text, &error);
        free(error);
        error = NULL;
        if (probe == NULL) return 0;
        zj_free(probe);
        value_json = zeno_strdup(value_text);
    } else {
        value_json = zeno_json_escape(value_text);
    }
    if (value_json == NULL) return 0;
    json = zeno_settings_list(store_path);
    if (json == NULL) {
        free(value_json);
        return 0;
    }
    root = zj_parse(json, &error);
    free(error);
    free(json);
    if (root == NULL || root->type != ZJ_OBJECT) {
        zj_free(root);
        free(value_json);
        return 0;
    }
    result = zeno_strdup("{");
    if (result == NULL) {
        zj_free(root);
        free(value_json);
        return 0;
    }
    /* Re-emit every known section first (stable order), then any extras. */
    for (size_t s = 0; s < sizeof(known) / sizeof(known[0]); s++) {
        ZjNode *sec = zj_object_get(root, known[s]);
        char *sec_out = zeno_strdup("{");
        int first_key = 1;
        if (sec_out == NULL) continue;
        if (sec != NULL && sec->type == ZJ_OBJECT) {
            for (ZjPair *pair = sec->object; pair != NULL; pair = pair->next) {
                int is_target = strcmp(known[s], section) == 0 && strcmp(pair->key, key) == 0;
                char *item = NULL;
                char *next = NULL;
                char *val = NULL;
                if (is_target) continue; /* replaced below */
                val = zj_stringify_compact(pair->value);
                if (val == NULL) continue;
                {
                    char *kesc = zeno_json_escape(pair->key);
                    item = (kesc != NULL) ? zeno_format("%s%s:%s", first_key ? "" : ",", kesc, val) : NULL;
                    free(kesc);
                }
                free(val);
                if (item == NULL) continue;
                next = zeno_format("%s%s", sec_out, item);
                free(item);
                free(sec_out);
                sec_out = next;
                if (sec_out == NULL) break;
                first_key = 0;
            }
        }
        if (sec_out == NULL) continue;
        if (strcmp(known[s], section) == 0) {
            char *kesc = zeno_json_escape(key);
            char *item = (kesc != NULL) ? zeno_format("%s%s:%s", first_key ? "" : ",", kesc, value_json) : NULL;
            char *next = NULL;
            free(kesc);
            if (item != NULL) {
                next = zeno_format("%s%s", sec_out, item);
                free(item);
            }
            free(sec_out);
            sec_out = next;
            if (sec_out == NULL) continue;
        }
        {
            char *closed = zeno_format("%s}", sec_out);
            char *kesc = zeno_json_escape(known[s]);
            char *item = (kesc != NULL && closed != NULL) ? zeno_format("%s%s:%s", s == 0 ? "" : ",", kesc, closed) : NULL;
            char *next = NULL;
            free(kesc);
            free(closed);
            free(sec_out);
            if (item == NULL) continue;
            next = zeno_format("%s%s", result, item);
            free(item);
            free(result);
            result = next;
            if (result == NULL) break;
        }
    }
    /* Unknown pre-existing sections are preserved verbatim. */
    if (result != NULL && root->type == ZJ_OBJECT) {
        for (ZjPair *pair = root->object; pair != NULL; pair = pair->next) {
            int known_section = 0;
            for (size_t s = 0; s < sizeof(known) / sizeof(known[0]); s++)
                if (strcmp(pair->key, known[s]) == 0) known_section = 1;
            if (known_section) continue;
            {
                char *val = zj_stringify_compact(pair->value);
                char *kesc = zeno_json_escape(pair->key);
                char *item = (val != NULL && kesc != NULL) ? zeno_format(",%s:%s", kesc, val) : NULL;
                char *next = NULL;
                free(val);
                free(kesc);
                if (item == NULL) continue;
                next = zeno_format("%s%s", result, item);
                free(item);
                free(result);
                result = next;
                if (result == NULL) break;
            }
        }
    }
    if (result != NULL) {
        char *closed = zeno_format("%s}", result);
        free(result);
        result = closed;
    }
    if (result != NULL) {
        ok = studio_store_write(store_path, "Zeno Settings", result);
    }
    free(result);
    free(value_json);
    zj_free(root);
    return ok;
}

/* ---------------- projects ---------------- */

static char *studio_folders_to_json(const char *folders_csv) {
    char *array = zeno_strdup("[");
    size_t added = 0;
    const char *cursor;
    if (array == NULL) return NULL;
    if (folders_csv == NULL || *folders_csv == '\0') {
        char *closed = zeno_format("%s]", array);
        free(array);
        return closed;
    }
    cursor = folders_csv;
    while (*cursor != '\0' && added < 32) {
        const char *end = cursor;
        char *piece;
        char *trimmed;
        char *esc = NULL;
        char *item = NULL;
        char *next = NULL;
        while (*end != '\0' && *end != ',' && *end != ';' && *end != '\n') end++;
        piece = zeno_strndup(cursor, (size_t)(end - cursor));
        trimmed = piece != NULL ? zeno_trim_copy(piece) : NULL;
        free(piece);
        if (trimmed != NULL && *trimmed != '\0' && strlen(trimmed) <= 256) {
            esc = zeno_json_escape(trimmed);
            if (esc != NULL) {
                item = zeno_format("%s%s", added == 0 ? "" : ",", esc);
                if (item != NULL) {
                    next = zeno_format("%s%s", array, item);
                    free(item);
                    free(array);
                    array = next;
                    if (array != NULL) added++;
                }
            }
            free(esc);
        }
        free(trimmed);
        if (array == NULL) return NULL;
        cursor = (*end != '\0') ? end + 1 : end;
    }
    {
        char *closed = zeno_format("%s]", array);
        free(array);
        return closed;
    }
}

char *zeno_projects_list(const char *store_path) {
    char *json;
    if (store_path == NULL || *store_path == '\0') return zeno_strdup("{\"projects\":[]}");
    json = studio_store_read(store_path, "{\"projects\":[]}");
    if (json == NULL) return zeno_strdup("{\"projects\":[]}");
    {
        char *error = NULL;
        ZjNode *root = zj_parse(json, &error);
        free(error);
        if (root == NULL) {
            free(json);
            return zeno_strdup("{\"projects\":[]}");
        }
        zj_free(root);
    }
    return json;
}

char *zeno_project_create(const char *store_path, const char *name, const char *folders_csv) {
    char *trimmed;
    char *folders_json;
    char *id = NULL;
    char *list_json = NULL;
    char *error = NULL;
    ZjNode *root = NULL;
    ZjNode *arr = NULL;
    char *result = NULL;
    char *item = NULL;
    if (store_path == NULL || name == NULL) return NULL;
    trimmed = zeno_trim_copy(name);
    if (trimmed == NULL || *trimmed == '\0' || strlen(trimmed) > 120) {
        free(trimmed);
        return NULL;
    }
    folders_json = studio_folders_to_json(folders_csv);
    if (folders_json == NULL) {
        free(trimmed);
        return NULL;
    }
    id = studio_make_id("prj");
    if (id == NULL) {
        free(trimmed);
        free(folders_json);
        return NULL;
    }
    list_json = zeno_projects_list(store_path);
    if (list_json == NULL) {
        free(trimmed);
        free(folders_json);
        free(id);
        return NULL;
    }
    root = zj_parse(list_json, &error);
    free(error);
    if (root == NULL) {
        free(trimmed);
        free(folders_json);
        free(id);
        free(list_json);
        return NULL;
    }
    {
        char *name_esc = zeno_json_escape(trimmed);
        char *id_esc = zeno_json_escape(id);
        long long now_ms = zeno_now_ms();
        item = (name_esc != NULL && id_esc != NULL)
                   ? zeno_format("{\"id\":%s,\"name\":%s,\"folders\":%s,"
                                 "\"created_ms\":%lld,\"updated_ms\":%lld}",
                                 id_esc, name_esc, folders_json, now_ms, now_ms)
                   : NULL;
        free(name_esc);
        free(id_esc);
    }
    if (item == NULL) {
        zj_free(root);
        free(trimmed);
        free(folders_json);
        free(id);
        free(list_json);
        return NULL;
    }
    {
        char *array = zeno_strdup("[");
        if (array != NULL) {
            arr = zj_object_get(root, "projects");
            if (arr != NULL) {
                for (size_t i = 0; i < arr->count; i++) {
                    ZjNode *entry = zj_array_get(arr, i);
                    char *compact = zj_stringify_compact(entry);
                    char *next = NULL;
                    char *piece = NULL;
                    if (compact == NULL) continue;
                    piece = zeno_format("%s%s", i == 0 ? "" : ",", compact);
                    free(compact);
                    if (piece == NULL) continue;
                    next = zeno_format("%s%s", array, piece);
                    free(piece);
                    free(array);
                    array = next;
                    if (array == NULL) break;
                }
            }
            if (array != NULL) {
                char *with_new = zeno_format("%s%s%s]", array, (arr != NULL && arr->count > 0) ? "," : "", item);
                free(array);
                array = with_new;
            }
            if (array != NULL) {
                char *doc = zeno_format("{\"projects\":%s}", array);
                free(array);
                if (doc != NULL) {
                    if (studio_store_write(store_path, "Zeno Projects", doc)) result = item;
                    else free(item);
                    free(doc);
                }
            }
        }
    }
    zj_free(root);
    free(trimmed);
    free(folders_json);
    free(id);
    free(list_json);
    return result;
}

static int studio_projects_rewrite(const char *store_path, const char *target_id,
                                   const char *new_name, const char *new_folders_json,
                                   int drop) {
    char *list_json = zeno_projects_list(store_path);
    char *error = NULL;
    ZjNode *root = NULL;
    ZjNode *arr = NULL;
    char *array = NULL;
    char *doc = NULL;
    int found = 0;
    int ok = 0;
    if (list_json == NULL) return 0;
    root = zj_parse(list_json, &error);
    free(error);
    free(list_json);
    if (root == NULL) return 0;
    array = zeno_strdup("[");
    if (array == NULL) {
        zj_free(root);
        return 0;
    }
    arr = zj_object_get(root, "projects");
    if (arr != NULL) {
        size_t kept = 0;
        for (size_t i = 0; i < arr->count; i++) {
            ZjNode *entry = zj_array_get(arr, i);
            const char *eid = zj_string(zj_object_get(entry, "id"));
            int is_target = eid != NULL && target_id != NULL && strcmp(eid, target_id) == 0;
            char *compact = NULL;
            char *piece = NULL;
            char *next = NULL;
            if (is_target) found = 1;
            if (is_target && drop) continue;
            if (is_target && (new_name != NULL || new_folders_json != NULL)) {
                const char *old_name = zj_string(zj_object_get(entry, "name"));
                long long created = zj_integer(zj_object_get(entry, "created_ms"), zeno_now_ms());
                ZjNode *folders_node = zj_object_get(entry, "folders");
                char *folders_current = folders_node != NULL ? zj_stringify_compact(folders_node) : NULL;
                const char *folders_use = new_folders_json != NULL ? new_folders_json
                                                                   : (folders_current != NULL ? folders_current : "[]");
                char *name_esc = zeno_json_escape(new_name != NULL ? new_name : (old_name != NULL ? old_name : ""));
                char *id_esc = zeno_json_escape(eid != NULL ? eid : "");
                compact = (name_esc != NULL && id_esc != NULL)
                              ? zeno_format("{\"id\":%s,\"name\":%s,\"folders\":%s,"
                                            "\"created_ms\":%lld,\"updated_ms\":%lld}",
                                            id_esc, name_esc, folders_use, created, zeno_now_ms())
                              : NULL;
                free(name_esc);
                free(id_esc);
                free(folders_current);
            } else {
                compact = zj_stringify_compact(entry);
            }
            if (compact == NULL) continue;
            piece = zeno_format("%s%s", kept == 0 ? "" : ",", compact);
            free(compact);
            if (piece == NULL) continue;
            next = zeno_format("%s%s", array, piece);
            free(piece);
            free(array);
            array = next;
            if (array == NULL) break;
            kept++;
        }
    }
    if (array != NULL) {
        char *closed = zeno_format("%s]", array);
        free(array);
        array = closed;
    }
    if (array != NULL) doc = zeno_format("{\"projects\":%s}", array);
    free(array);
    if (doc != NULL) {
        ok = found && studio_store_write(store_path, "Zeno Projects", doc);
        free(doc);
    }
    zj_free(root);
    return ok;
}

int zeno_project_rename(const char *store_path, const char *id, const char *new_name) {
    char *trimmed;
    int ok;
    if (store_path == NULL || id == NULL || new_name == NULL) return 0;
    if (!studio_valid_id(id)) return 0;
    trimmed = zeno_trim_copy(new_name);
    if (trimmed == NULL || *trimmed == '\0' || strlen(trimmed) > 120) {
        free(trimmed);
        return 0;
    }
    ok = studio_projects_rewrite(store_path, id, trimmed, NULL, 0);
    free(trimmed);
    return ok;
}

int zeno_project_delete(const char *store_path, const char *id) {
    if (store_path == NULL || id == NULL || !studio_valid_id(id)) return 0;
    return studio_projects_rewrite(store_path, id, NULL, NULL, 1);
}

int zeno_project_set_folders(const char *store_path, const char *id, const char *folders_csv) {
    char *folders_json;
    int ok;
    if (store_path == NULL || id == NULL || !studio_valid_id(id)) return 0;
    folders_json = studio_folders_to_json(folders_csv);
    if (folders_json == NULL) return 0;
    ok = studio_projects_rewrite(store_path, id, NULL, folders_json, 0);
    free(folders_json);
    return ok;
}

/* ---------------- remotes ---------------- */

static int studio_normalize_remote_type(const char *raw, char *out, size_t size) {
    if (studio_streq_ci(raw, "vps")) {
        zeno_copy_string(out, size, "vps");
        return 1;
    }
    if (studio_streq_ci(raw, "vm") || studio_streq_ci(raw, "virtual-machine") ||
        studio_streq_ci(raw, "virtual_machine")) {
        zeno_copy_string(out, size, "vm");
        return 1;
    }
    if (studio_streq_ci(raw, "colab") || studio_streq_ci(raw, "google-colab") ||
        studio_streq_ci(raw, "google_colab")) {
        zeno_copy_string(out, size, "colab");
        return 1;
    }
    return 0;
}

char *zeno_remotes_list(const char *store_path) {
    char *json;
    if (store_path == NULL || *store_path == '\0') return zeno_strdup("{\"remotes\":[]}");
    json = studio_store_read(store_path, "{\"remotes\":[]}");
    if (json == NULL) return zeno_strdup("{\"remotes\":[]}");
    {
        char *error = NULL;
        ZjNode *root = zj_parse(json, &error);
        free(error);
        if (root == NULL) {
            free(json);
            return zeno_strdup("{\"remotes\":[]}");
        }
        zj_free(root);
    }
    return json;
}

char *zeno_remote_add(const char *store_path, const char *type, const char *name,
                      const char *target) {
    char rtype[16];
    char *trimmed_name = NULL;
    char *trimmed_target = NULL;
    char host[256] = "";
    char user[128] = "root";
    long port = 22;
    char *url = NULL;
    char *id = NULL;
    char *list_json = NULL;
    char *error = NULL;
    ZjNode *root = NULL;
    char *item = NULL;
    char *result = NULL;
    if (store_path == NULL || type == NULL || name == NULL || target == NULL) return NULL;
    if (!studio_normalize_remote_type(type, rtype, sizeof(rtype))) return NULL;
    trimmed_name = zeno_trim_copy(name);
    trimmed_target = zeno_trim_copy(target);
    if (trimmed_name == NULL || *trimmed_name == '\0' || strlen(trimmed_name) > 80 ||
        trimmed_target == NULL || *trimmed_target == '\0' || strlen(trimmed_target) > 512) {
        free(trimmed_name);
        free(trimmed_target);
        return NULL;
    }
    if (strcmp(rtype, "colab") == 0) {
        if (!zeno_contains_ci(trimmed_target, "http")) {
            free(trimmed_name);
            free(trimmed_target);
            return NULL;
        }
        zeno_copy_string(host, sizeof(host), "colab");
        url = zeno_strdup(trimmed_target);
        if (url == NULL) {
            free(trimmed_name);
            free(trimmed_target);
            return NULL;
        }
    } else {
        /* [user@]host[:port] */
        char *at = strrchr(trimmed_target, '@');
        char *hostport = trimmed_target;
        char *colon;
        if (at != NULL) {
            *at = '\0';
            hostport = at + 1;
            if (*trimmed_target != '\0') zeno_copy_string(user, sizeof(user), trimmed_target);
        }
        colon = strrchr(hostport, ':');
        if (colon != NULL) {
            char *end = NULL;
            long parsed;
            *colon = '\0';
            parsed = strtol(colon + 1, &end, 10);
            if (end != NULL && *end == '\0' && parsed > 0 && parsed < 65536) port = parsed;
        }
        if (*hostport == '\0' || strlen(hostport) > 255 || strchr(hostport, ' ') != NULL) {
            free(trimmed_name);
            free(trimmed_target);
            free(url);
            return NULL;
        }
        zeno_copy_string(host, sizeof(host), hostport);
        url = zeno_strdup("");
        if (url == NULL) {
            free(trimmed_name);
            free(trimmed_target);
            return NULL;
        }
    }
    id = studio_make_id("rem");
    list_json = zeno_remotes_list(store_path);
    if (id == NULL || list_json == NULL) {
        free(trimmed_name);
        free(trimmed_target);
        free(url);
        free(id);
        free(list_json);
        return NULL;
    }
    root = zj_parse(list_json, &error);
    free(error);
    if (root == NULL) {
        free(trimmed_name);
        free(trimmed_target);
        free(url);
        free(id);
        free(list_json);
        return NULL;
    }
    {
        char *id_esc = zeno_json_escape(id);
        char *name_esc = zeno_json_escape(trimmed_name);
        char *type_esc = zeno_json_escape(rtype);
        char *host_esc = zeno_json_escape(host);
        char *user_esc = zeno_json_escape(user);
        char *url_esc = zeno_json_escape(url);
        long long now_ms = zeno_now_ms();
        item = (id_esc != NULL && name_esc != NULL && type_esc != NULL && host_esc != NULL &&
                user_esc != NULL && url_esc != NULL)
                   ? zeno_format("{\"id\":%s,\"name\":%s,\"type\":%s,\"host\":%s,"
                                 "\"user\":%s,\"port\":%ld,\"url\":%s,\"created_ms\":%lld}",
                                 id_esc, name_esc, type_esc, host_esc, user_esc, port, url_esc, now_ms)
                   : NULL;
        free(id_esc);
        free(name_esc);
        free(type_esc);
        free(host_esc);
        free(user_esc);
        free(url_esc);
    }
    if (item != NULL) {
        ZjNode *arr = zj_object_get(root, "remotes");
        char *array = zeno_strdup("[");
        if (array != NULL) {
            if (arr != NULL) {
                for (size_t i = 0; i < arr->count; i++) {
                    char *compact = zj_stringify_compact(zj_array_get(arr, i));
                    char *piece = NULL;
                    char *next = NULL;
                    if (compact == NULL) continue;
                    piece = zeno_format("%s%s", i == 0 ? "" : ",", compact);
                    free(compact);
                    if (piece == NULL) continue;
                    next = zeno_format("%s%s", array, piece);
                    free(piece);
                    free(array);
                    array = next;
                    if (array == NULL) break;
                }
            }
            if (array != NULL) {
                char *with_new = zeno_format("%s%s%s]", array,
                                             (arr != NULL && arr->count > 0) ? "," : "", item);
                free(array);
                array = with_new;
            }
            if (array != NULL) {
                char *doc = zeno_format("{\"remotes\":%s}", array);
                free(array);
                if (doc != NULL) {
                    if (studio_store_write(store_path, "Zeno Remote Instances", doc)) result = item;
                    else free(item);
                    free(doc);
                }
            }
        }
        if (result == NULL) free(item);
    }
    zj_free(root);
    free(trimmed_name);
    free(trimmed_target);
    free(url);
    free(id);
    free(list_json);
    return result;
}

static int studio_remotes_rewrite(const char *store_path, const char *target_id,
                                  const char *new_name, int drop) {
    char *list_json = zeno_remotes_list(store_path);
    char *error = NULL;
    ZjNode *root = NULL;
    char *array = NULL;
    char *doc = NULL;
    int found = 0;
    int ok = 0;
    if (list_json == NULL) return 0;
    root = zj_parse(list_json, &error);
    free(error);
    free(list_json);
    if (root == NULL) return 0;
    array = zeno_strdup("[");
    if (array == NULL) {
        zj_free(root);
        return 0;
    }
    {
        ZjNode *arr = zj_object_get(root, "remotes");
        size_t kept = 0;
        if (arr != NULL) {
            for (size_t i = 0; i < arr->count; i++) {
                ZjNode *entry = zj_array_get(arr, i);
                const char *eid = zj_string(zj_object_get(entry, "id"));
                int is_target = eid != NULL && target_id != NULL && strcmp(eid, target_id) == 0;
                char *compact = NULL;
                char *piece = NULL;
                char *next = NULL;
                if (is_target) found = 1;
                if (is_target && drop) continue;
                if (is_target && new_name != NULL) {
                    const char *type = zj_string(zj_object_get(entry, "type"));
                    const char *host = zj_string(zj_object_get(entry, "host"));
                    const char *user = zj_string(zj_object_get(entry, "user"));
                    const char *url = zj_string(zj_object_get(entry, "url"));
                    long long created = zj_integer(zj_object_get(entry, "created_ms"), zeno_now_ms());
                    long long port = zj_integer(zj_object_get(entry, "port"), 22);
                    char *id_esc = zeno_json_escape(eid != NULL ? eid : "");
                    char *name_esc = zeno_json_escape(new_name);
                    char *type_esc = zeno_json_escape(type != NULL ? type : "vps");
                    char *host_esc = zeno_json_escape(host != NULL ? host : "");
                    char *user_esc = zeno_json_escape(user != NULL ? user : "root");
                    char *url_esc = zeno_json_escape(url != NULL ? url : "");
                    compact = (id_esc != NULL && name_esc != NULL && type_esc != NULL &&
                               host_esc != NULL && user_esc != NULL && url_esc != NULL)
                                  ? zeno_format("{\"id\":%s,\"name\":%s,\"type\":%s,\"host\":%s,"
                                                "\"user\":%s,\"port\":%lld,\"url\":%s,\"created_ms\":%lld}",
                                                id_esc, name_esc, type_esc, host_esc, user_esc, port,
                                                url_esc, created)
                                  : NULL;
                    free(id_esc);
                    free(name_esc);
                    free(type_esc);
                    free(host_esc);
                    free(user_esc);
                    free(url_esc);
                } else {
                    compact = zj_stringify_compact(entry);
                }
                if (compact == NULL) continue;
                piece = zeno_format("%s%s", kept == 0 ? "" : ",", compact);
                free(compact);
                if (piece == NULL) continue;
                next = zeno_format("%s%s", array, piece);
                free(piece);
                free(array);
                array = next;
                if (array == NULL) break;
                kept++;
            }
        }
    }
    if (array != NULL) {
        char *closed = zeno_format("%s]", array);
        free(array);
        array = closed;
    }
    if (array != NULL) doc = zeno_format("{\"remotes\":%s}", array);
    free(array);
    if (doc != NULL) {
        ok = found && studio_store_write(store_path, "Zeno Remote Instances", doc);
        free(doc);
    }
    zj_free(root);
    return ok;
}

int zeno_remote_remove(const char *store_path, const char *id) {
    if (store_path == NULL || id == NULL || !studio_valid_id(id)) return 0;
    return studio_remotes_rewrite(store_path, id, NULL, 1);
}

int zeno_remote_rename(const char *store_path, const char *id, const char *new_name) {
    char *trimmed;
    int ok;
    if (store_path == NULL || id == NULL || new_name == NULL || !studio_valid_id(id)) return 0;
    trimmed = zeno_trim_copy(new_name);
    if (trimmed == NULL || *trimmed == '\0' || strlen(trimmed) > 80) {
        free(trimmed);
        return 0;
    }
    ok = studio_remotes_rewrite(store_path, id, trimmed, 0);
    free(trimmed);
    return ok;
}

char *zeno_remote_connect_cmd(const char *store_path, const char *id) {
    char *list_json;
    char *error = NULL;
    ZjNode *root;
    char *result = NULL;
    if (store_path == NULL || id == NULL) return NULL;
    list_json = zeno_remotes_list(store_path);
    if (list_json == NULL) return NULL;
    root = zj_parse(list_json, &error);
    free(error);
    free(list_json);
    if (root == NULL) return NULL;
    {
        ZjNode *arr = zj_object_get(root, "remotes");
        if (arr != NULL) {
            for (size_t i = 0; i < arr->count; i++) {
                ZjNode *entry = zj_array_get(arr, i);
                const char *eid = zj_string(zj_object_get(entry, "id"));
                if (eid == NULL || strcmp(eid, id) != 0) continue;
                {
                    const char *type = zj_string(zj_object_get(entry, "type"));
                    const char *host = zj_string(zj_object_get(entry, "host"));
                    const char *user = zj_string(zj_object_get(entry, "user"));
                    const char *url = zj_string(zj_object_get(entry, "url"));
                    long long port = zj_integer(zj_object_get(entry, "port"), 22);
                    if (type != NULL && strcmp(type, "colab") == 0) {
                        char *url_esc = zeno_json_escape(url != NULL ? url : "");
                        result = (url_esc != NULL)
                                     ? zeno_format("{\"kind\":\"colab\",\"open_url\":%s,"
                                                   "\"note\":\"open the Colab URL and copy the SSH command\"}",
                                                   url_esc)
                                     : NULL;
                        free(url_esc);
                    } else {
                        char *cmd = (port != 22)
                                        ? zeno_format("ssh -p %lld %s@%s", port,
                                                      user != NULL ? user : "root",
                                                      host != NULL ? host : "")
                                        : zeno_format("ssh %s@%s", user != NULL ? user : "root",
                                                      host != NULL ? host : "");
                        char *cmd_esc = cmd != NULL ? zeno_json_escape(cmd) : NULL;
                        free(cmd);
                        result = (cmd_esc != NULL)
                                     ? zeno_format("{\"kind\":%s,\"command\":%s}",
                                                   (type != NULL && strcmp(type, "vm") == 0) ? "\"vm\""
                                                                                             : "\"vps\"",
                                                   cmd_esc)
                                     : NULL;
                        free(cmd_esc);
                    }
                    break;
                }
            }
        }
    }
    zj_free(root);
    return result;
}

/* ---------------- plugins ---------------- */

static int studio_is_slug(const char *id) {
    size_t len;
    if (id == NULL) return 0;
    len = strlen(id);
    if (len < 2 || len > 48) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '-')) return 0;
    }
    return 1;
}

int zeno_plugin_validate(const char *plugin_json, char *error, size_t error_size) {
    char *parse_error = NULL;
    ZjNode *root;
    const char *id;
    const char *name;
    const char *version;
    ZjNode *node;
    if (error != NULL && error_size > 0) error[0] = '\0';
    if (plugin_json == NULL || strlen(plugin_json) > 65536) {
        zeno_set_error(error, error_size, "plugin JSON is missing or exceeds 64KB.");
        return 0;
    }
    root = zj_parse(plugin_json, &parse_error);
    free(parse_error);
    if (root == NULL || root->type != ZJ_OBJECT) {
        zj_free(root);
        zeno_set_error(error, error_size, "plugin JSON must be an object.");
        return 0;
    }
    id = zj_string(zj_object_get(root, "id"));
    name = zj_string(zj_object_get(root, "name"));
    version = zj_string(zj_object_get(root, "version"));
    if (!studio_is_slug(id)) {
        zj_free(root);
        zeno_set_error(error, error_size, "field 'id' must be a slug [a-z0-9-] (2..48).");
        return 0;
    }
    if (name == NULL || *name == '\0' || strlen(name) > 80) {
        zj_free(root);
        zeno_set_error(error, error_size, "field 'name' is required (1..80).");
        return 0;
    }
    if (version == NULL || *version == '\0' || strlen(version) > 16) {
        zj_free(root);
        zeno_set_error(error, error_size, "field 'version' is required (e.g. 1.0.0).");
        return 0;
    }
    node = zj_object_get(root, "functions");
    if (node != NULL) {
        if (node->type != ZJ_ARRAY || node->count > 32) {
            zj_free(root);
            zeno_set_error(error, error_size, "field 'functions' must be an array (max 32).");
            return 0;
        }
        for (size_t i = 0; i < node->count; i++) {
            ZjNode *fn = zj_array_get(node, i);
            if (fn == NULL || fn->type != ZJ_OBJECT ||
                zj_string(zj_object_get(fn, "name")) == NULL ||
                zj_string(zj_object_get(fn, "description")) == NULL) {
                zj_free(root);
                zeno_set_error(error, error_size, "each function needs name+description.");
                return 0;
            }
        }
    }
    node = zj_object_get(root, "buttons");
    if (node != NULL) {
        if (node->type != ZJ_ARRAY || node->count > 32) {
            zj_free(root);
            zeno_set_error(error, error_size, "field 'buttons' must be an array (max 32).");
            return 0;
        }
        for (size_t i = 0; i < node->count; i++) {
            ZjNode *btn = zj_array_get(node, i);
            if (btn == NULL || btn->type != ZJ_OBJECT ||
                zj_string(zj_object_get(btn, "id")) == NULL ||
                zj_string(zj_object_get(btn, "label")) == NULL) {
                zj_free(root);
                zeno_set_error(error, error_size, "each button needs id+label.");
                return 0;
            }
        }
    }
    node = zj_object_get(root, "agents");
    if (node != NULL) {
        if (node->type != ZJ_ARRAY || node->count > 16) {
            zj_free(root);
            zeno_set_error(error, error_size, "field 'agents' must be an array (max 16).");
            return 0;
        }
        for (size_t i = 0; i < node->count; i++) {
            ZjNode *agent = zj_array_get(node, i);
            if (agent == NULL || agent->type != ZJ_OBJECT ||
                zj_string(zj_object_get(agent, "name")) == NULL ||
                zj_string(zj_object_get(agent, "prompt")) == NULL) {
                zj_free(root);
                zeno_set_error(error, error_size, "each agent needs name+prompt.");
                return 0;
            }
        }
    }
    node = zj_object_get(root, "ui");
    if (node != NULL && node->type != ZJ_OBJECT) {
        zj_free(root);
        zeno_set_error(error, error_size, "field 'ui' must be an object.");
        return 0;
    }
    zj_free(root);
    return 1;
}

char *zeno_plugin_create(const char *user_request, const char *name_hint) {
    char slug[64];
    char *request_trim;
    char *desc;
    char *id_esc;
    char *name_esc;
    char *desc_esc;
    char *fn_name;
    char *result;
    const char *req;
    if (user_request == NULL || *user_request == '\0') return NULL;
    if (name_hint != NULL && *name_hint != '\0') {
        studio_slugify(name_hint, slug, sizeof(slug));
    } else {
        char head[64];
        size_t n = 0;
        const char *p = user_request;
        while (*p != '\0' && n + 1 < sizeof(head)) {
            if (isalnum((unsigned char)*p) || *p == ' ' || *p == '-') head[n++] = *p;
            p++;
            if (n > 32) break;
        }
        head[n] = '\0';
        studio_slugify(head, slug, sizeof(slug));
    }
    request_trim = zeno_trim_copy(user_request);
    if (request_trim == NULL) return NULL;
    if (strlen(request_trim) > 400) request_trim[400] = '\0';
    desc = zeno_trim_copy(request_trim);
    req = request_trim;
    {
        char display[84];
        size_t len = strlen(req);
        if (name_hint != NULL && *name_hint != '\0') {
            zeno_copy_string(display, sizeof(display), name_hint);
        } else {
            size_t copy = len > 40 ? 40 : len;
            memcpy(display, req, copy);
            display[copy] = '\0';
            if (len > 40) {
                size_t cur = strlen(display);
                if (cur + 1 < sizeof(display)) {
                    display[cur++] = '.';
                    display[cur++] = '.';
                    display[cur++] = '.';
                    display[cur] = '\0';
                }
            }
            if (display[0] != '\0') display[0] = (char)toupper((unsigned char)display[0]);
        }
        id_esc = zeno_json_escape(slug);
        name_esc = zeno_json_escape(display);
        desc_esc = zeno_json_escape(desc != NULL ? desc : req);
        fn_name = zeno_format("%s_run", slug);
        if (fn_name != NULL) {
            for (char *p = fn_name; *p != '\0'; p++)
                if (*p == '-') *p = '_';
        }
        result = NULL;
        {
            char *fn_esc = zeno_json_escape(fn_name != NULL ? fn_name : "plugin_run");
            if (id_esc != NULL && name_esc != NULL && desc_esc != NULL && fn_esc != NULL) {
                result = zeno_format("{\"id\":%s,\"name\":%s,\"version\":\"1.0.0\","
                                     "\"enabled\":true,\"description\":%s,"
                                     "\"functions\":[{\"name\":%s,\"description\":%s,"
                                     "\"parameters\":{\"type\":\"object\",\"properties\":{"
                                     "\"input\":{\"type\":\"string\"}}}}],"
                                     "\"buttons\":[{\"id\":%s,\"label\":%s,"
                                     "\"icon\":\"oc-puzzle-2\",\"action\":%s}],"
                                     "\"agents\":[{\"name\":%s,\"prompt\":%s,\"model\":\"\"}],"
                                     "\"ui\":{\"css\":\"\",\"theme\":\"auto\"}}",
                                     id_esc, name_esc, desc_esc, fn_esc, desc_esc, id_esc,
                                     name_esc, id_esc, name_esc, desc_esc);
            }
            free(fn_esc);
        }
        free(fn_name);
        free(id_esc);
        free(name_esc);
        free(desc_esc);
    }
    free(request_trim);
    free(desc);
    return result;
}

static char *studio_plugin_path(const char *dir, const char *id) {
    char *leaf;
    char *full;
    if (dir == NULL || id == NULL || !studio_is_slug(id)) return NULL;
    leaf = zeno_format("%s.json", id);
    if (leaf == NULL) return NULL;
    full = zeno_join_path(dir, leaf);
    free(leaf);
    return full;
}

static int studio_scan_dir(char ***out_names, size_t *out_count, const char *dir) {
    *out_names = NULL;
    *out_count = 0;
#ifdef _WIN32
    {
        char *pattern = zeno_join_path(dir, "*.json");
        WIN32_FIND_DATAA data;
        HANDLE handle;
        char **names = NULL;
        size_t count = 0;
        size_t capacity = 0;
        if (pattern == NULL) return 0;
        handle = FindFirstFileA(pattern, &data);
        free(pattern);
        if (handle == INVALID_HANDLE_VALUE) return 1;
        do {
            char *copy;
            char **grown;
            if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
            if (count == capacity) {
                size_t next = capacity == 0 ? 16 : capacity * 2;
                grown = (char **)realloc(names, next * sizeof(*grown));
                if (grown == NULL) break;
                names = grown;
                capacity = next;
            }
            copy = zeno_strdup(data.cFileName);
            if (copy == NULL) break;
            names[count++] = copy;
        } while (FindNextFileA(handle, &data) != 0);
        FindClose(handle);
        *out_names = names;
        *out_count = count;
        return 1;
    }
#else
    {
        DIR *handle = opendir(dir);
        char **names = NULL;
        size_t count = 0;
        size_t capacity = 0;
        struct dirent *entry;
        if (handle == NULL) return 1;
        while ((entry = readdir(handle)) != NULL) {
            size_t len = strlen(entry->d_name);
            char **grown;
            char *copy;
            if (len < 6 || strcmp(entry->d_name + len - 5, ".json") != 0) continue;
            if (count == capacity) {
                size_t next = capacity == 0 ? 16 : capacity * 2;
                grown = (char **)realloc(names, next * sizeof(*grown));
                if (grown == NULL) break;
                names = grown;
                capacity = next;
            }
            copy = zeno_strdup(entry->d_name);
            if (copy == NULL) break;
            names[count++] = copy;
        }
        closedir(handle);
        *out_names = names;
        *out_count = count;
        return 1;
    }
#endif
}

int zeno_plugin_save(const char *plugins_dir, const char *plugin_json) {
    char err[256];
    char *error = NULL;
    ZjNode *root = NULL;
    const char *id;
    char *path = NULL;
    int ok = 0;
    if (plugins_dir == NULL || plugin_json == NULL) return 0;
    if (!zeno_plugin_validate(plugin_json, err, sizeof(err))) return 0;
    if (!zeno_mkdirs(plugins_dir)) return 0;
    root = zj_parse(plugin_json, &error);
    free(error);
    if (root == NULL) return 0;
    id = zj_string(zj_object_get(root, "id"));
    path = studio_plugin_path(plugins_dir, id != NULL ? id : "");
    if (path != NULL) ok = zeno_write_file_atomic(path, plugin_json);
    free(path);
    zj_free(root);
    return ok;
}

char *zeno_plugin_list(const char *plugins_dir) {
    char **names = NULL;
    size_t count = 0;
    char *array = zeno_strdup("[");
    char *doc = NULL;
    size_t kept = 0;
    if (array == NULL) return NULL;
    if (plugins_dir == NULL || *plugins_dir == '\0') {
        doc = zeno_format("{\"plugins\":%s]}", "[]");
        free(array);
        return doc;
    }
    (void)studio_scan_dir(&names, &count, plugins_dir);
    for (size_t i = 0; i < count; i++) {
        char *full = zeno_join_path(plugins_dir, names[i]);
        char *content = full != NULL ? zeno_read_file(full, 65536) : NULL;
        char *error = NULL;
        ZjNode *root = content != NULL ? zj_parse(content, &error) : NULL;
        free(error);
        free(full);
        free(content);
        if (root != NULL && root->type == ZJ_OBJECT) {
            const char *id = zj_string(zj_object_get(root, "id"));
            const char *name = zj_string(zj_object_get(root, "name"));
            const char *version = zj_string(zj_object_get(root, "version"));
            const char *desc = zj_string(zj_object_get(root, "description"));
            ZjNode *en = zj_object_get(root, "enabled");
            if (id != NULL && name != NULL) {
                char *id_esc = zeno_json_escape(id);
                char *name_esc = zeno_json_escape(name);
                char *ver_esc = zeno_json_escape(version != NULL ? version : "1.0.0");
                char *desc_esc = zeno_json_escape(desc != NULL ? desc : "");
                char *item = (id_esc != NULL && name_esc != NULL && ver_esc != NULL && desc_esc != NULL)
                                 ? zeno_format("%s{\"id\":%s,\"name\":%s,\"version\":%s,"
                                               "\"enabled\":%s,\"description\":%s}",
                                               kept == 0 ? "" : ",", id_esc, name_esc, ver_esc,
                                               (en != NULL && en->type == ZJ_BOOL && !en->boolean) ? "false" : "true",
                                               desc_esc)
                                 : NULL;
                char *next = NULL;
                free(id_esc);
                free(name_esc);
                free(ver_esc);
                free(desc_esc);
                if (item != NULL) {
                    next = zeno_format("%s%s", array, item);
                    free(item);
                }
                if (next != NULL) {
                    free(array);
                    array = next;
                    kept++;
                }
            }
        }
        zj_free(root);
    }
    for (size_t i = 0; i < count; i++) free(names[i]);
    free(names);
    {
        char *closed = zeno_format("%s]", array);
        free(array);
        array = closed;
    }
    if (array != NULL) doc = zeno_format("{\"plugins\":%s}", array);
    free(array);
    return doc;
}

static int studio_plugin_rewrite(const char *plugins_dir, const char *id, int set_enabled,
                                 int enabled_value, const char *new_name) {
    char *path;
    char *content;
    char *error = NULL;
    ZjNode *root;
    char *functions_json = NULL;
    char *buttons_json = NULL;
    char *agents_json = NULL;
    char *ui_json = NULL;
    char *doc = NULL;
    int ok = 0;
    if (plugins_dir == NULL || id == NULL || !studio_is_slug(id)) return 0;
    path = studio_plugin_path(plugins_dir, id);
    if (path == NULL) return 0;
    content = zeno_read_file(path, 65536);
    if (content == NULL) {
        free(path);
        return 0;
    }
    root = zj_parse(content, &error);
    free(error);
    free(content);
    if (root == NULL || root->type != ZJ_OBJECT) {
        zj_free(root);
        free(path);
        return 0;
    }
    {
        const char *name = zj_string(zj_object_get(root, "name"));
        const char *version = zj_string(zj_object_get(root, "version"));
        const char *desc = zj_string(zj_object_get(root, "description"));
        ZjNode *en = zj_object_get(root, "enabled");
        int enabled = set_enabled ? (enabled_value ? 1 : 0)
                                  : !((en != NULL && en->type == ZJ_BOOL && !en->boolean) ? 0 : 1);
        const char *final_name = new_name != NULL ? new_name : (name != NULL ? name : id);
        ZjNode *fn = zj_object_get(root, "functions");
        ZjNode *btn = zj_object_get(root, "buttons");
        ZjNode *ag = zj_object_get(root, "agents");
        ZjNode *ui = zj_object_get(root, "ui");
        char *id_esc = zeno_json_escape(id);
        char *name_esc = zeno_json_escape(final_name);
        char *ver_esc = zeno_json_escape(version != NULL ? version : "1.0.0");
        char *desc_esc = zeno_json_escape(desc != NULL ? desc : "");
        functions_json = fn != NULL ? zj_stringify_compact(fn) : NULL;
        buttons_json = btn != NULL ? zj_stringify_compact(btn) : NULL;
        agents_json = ag != NULL ? zj_stringify_compact(ag) : NULL;
        ui_json = ui != NULL ? zj_stringify_compact(ui) : NULL;
        if (id_esc != NULL && name_esc != NULL && ver_esc != NULL && desc_esc != NULL) {
            doc = zeno_format("{\"id\":%s,\"name\":%s,\"version\":%s,\"enabled\":%s,"
                              "\"description\":%s,\"functions\":%s,\"buttons\":%s,"
                              "\"agents\":%s,\"ui\":%s}",
                              id_esc, name_esc, ver_esc, enabled ? "true" : "false", desc_esc,
                              functions_json != NULL ? functions_json : "[]",
                              buttons_json != NULL ? buttons_json : "[]",
                              agents_json != NULL ? agents_json : "[]",
                              ui_json != NULL ? ui_json : "{}");
        }
        free(id_esc);
        free(name_esc);
        free(ver_esc);
        free(desc_esc);
    }
    free(functions_json);
    free(buttons_json);
    free(agents_json);
    free(ui_json);
    zj_free(root);
    if (doc != NULL) {
        char err[256];
        if (zeno_plugin_validate(doc, err, sizeof(err))) ok = zeno_write_file_atomic(path, doc);
        free(doc);
    }
    free(path);
    return ok;
}

int zeno_plugin_set_enabled(const char *plugins_dir, const char *id, int enabled) {
    return studio_plugin_rewrite(plugins_dir, id, 1, enabled ? 1 : 0, NULL);
}

int zeno_plugin_rename(const char *plugins_dir, const char *id, const char *new_name) {
    char *trimmed;
    int ok;
    if (new_name == NULL) return 0;
    trimmed = zeno_trim_copy(new_name);
    if (trimmed == NULL || *trimmed == '\0' || strlen(trimmed) > 80) {
        free(trimmed);
        return 0;
    }
    ok = studio_plugin_rewrite(plugins_dir, id, 0, 0, trimmed);
    free(trimmed);
    return ok;
}

char *zeno_plugin_apply(const char *plugins_dir) {
    char **names = NULL;
    size_t count = 0;
    char *functions = zeno_strdup("[");
    char *buttons = zeno_strdup("[");
    char *agents = zeno_strdup("[");
    char *css = zeno_strdup("[");
    char *doc = NULL;
    size_t nf = 0;
    size_t nb = 0;
    size_t na = 0;
    size_t nc = 0;
    if (functions == NULL || buttons == NULL || agents == NULL || css == NULL) {
        free(functions);
        free(buttons);
        free(agents);
        free(css);
        return NULL;
    }
    if (plugins_dir != NULL && *plugins_dir != '\0') (void)studio_scan_dir(&names, &count, plugins_dir);
    for (size_t i = 0; i < count; i++) {
        char *full = zeno_join_path(plugins_dir, names[i]);
        char *content = full != NULL ? zeno_read_file(full, 65536) : NULL;
        char *error = NULL;
        ZjNode *root = content != NULL ? zj_parse(content, &error) : NULL;
        ZjNode *en;
        free(error);
        free(full);
        free(content);
        if (root == NULL || root->type != ZJ_OBJECT) {
            zj_free(root);
            continue;
        }
        en = zj_object_get(root, "enabled");
        if (en != NULL && en->type == ZJ_BOOL && !en->boolean) {
            zj_free(root);
            continue;
        }
        {
            ZjNode *fn = zj_object_get(root, "functions");
            ZjNode *btn = zj_object_get(root, "buttons");
            ZjNode *ag = zj_object_get(root, "agents");
            ZjNode *ui = zj_object_get(root, "ui");
            if (fn != NULL && fn->type == ZJ_ARRAY) {
                for (size_t k = 0; k < fn->count && nf < 128; k++) {
                    char *compact = zj_stringify_compact(zj_array_get(fn, k));
                    char *piece = compact != NULL ? zeno_format("%s%s", nf == 0 ? "" : ",", compact) : NULL;
                    char *next = piece != NULL ? zeno_format("%s%s", functions, piece) : NULL;
                    free(compact);
                    free(piece);
                    if (next == NULL) continue;
                    free(functions);
                    functions = next;
                    nf++;
                }
            }
            if (btn != NULL && btn->type == ZJ_ARRAY) {
                for (size_t k = 0; k < btn->count && nb < 128; k++) {
                    char *compact = zj_stringify_compact(zj_array_get(btn, k));
                    char *piece = compact != NULL ? zeno_format("%s%s", nb == 0 ? "" : ",", compact) : NULL;
                    char *next = piece != NULL ? zeno_format("%s%s", buttons, piece) : NULL;
                    free(compact);
                    free(piece);
                    if (next == NULL) continue;
                    free(buttons);
                    buttons = next;
                    nb++;
                }
            }
            if (ag != NULL && ag->type == ZJ_ARRAY) {
                for (size_t k = 0; k < ag->count && na < 64; k++) {
                    char *compact = zj_stringify_compact(zj_array_get(ag, k));
                    char *piece = compact != NULL ? zeno_format("%s%s", na == 0 ? "" : ",", compact) : NULL;
                    char *next = piece != NULL ? zeno_format("%s%s", agents, piece) : NULL;
                    free(compact);
                    free(piece);
                    if (next == NULL) continue;
                    free(agents);
                    agents = next;
                    na++;
                }
            }
            if (ui != NULL && ui->type == ZJ_OBJECT) {
                const char *css_text = zj_string(zj_object_get(ui, "css"));
                if (css_text != NULL && *css_text != '\0' && nc < 32) {
                    char *esc = zeno_json_escape(css_text);
                    char *piece = esc != NULL ? zeno_format("%s%s", nc == 0 ? "" : ",", esc) : NULL;
                    char *next = piece != NULL ? zeno_format("%s%s", css, piece) : NULL;
                    free(esc);
                    free(piece);
                    if (next != NULL) {
                        free(css);
                        css = next;
                        nc++;
                    }
                }
            }
        }
        zj_free(root);
    }
    for (size_t i = 0; i < count; i++) free(names[i]);
    free(names);
    {
        char *f_closed = zeno_format("%s]", functions);
        char *b_closed = zeno_format("%s]", buttons);
        char *a_closed = zeno_format("%s]", agents);
        char *c_closed = zeno_format("%s]", css);
        free(functions);
        free(buttons);
        free(agents);
        free(css);
        if (f_closed != NULL && b_closed != NULL && a_closed != NULL && c_closed != NULL) {
            doc = zeno_format("{\"functions\":%s,\"buttons\":%s,\"agents\":%s,\"css\":%s}",
                              f_closed, b_closed, a_closed, c_closed);
        }
        free(f_closed);
        free(b_closed);
        free(a_closed);
        free(c_closed);
    }
    return doc;
}

const char *zeno_plugin_guide(void) {
    return "# Zeno plugin authoring (JSON, pure C backend)\n"
           "\n"
           "A plugin is ONE JSON object saved as `<plugins_dir>/<id>.json`.\n"
           "The agent MUST generate valid JSON, save it with `create_plugin`\n"
           "(auto-saves), then it becomes available via `zeno_plugin_apply`.\n"
           "\n"
           "## Required schema\n"
           "\n"
           "```json\n"
           "{\n"
           "  \"id\": \"my-plugin\",\n"
           "  \"name\": \"My Plugin\",\n"
           "  \"version\": \"1.0.0\",\n"
           "  \"enabled\": true,\n"
           "  \"description\": \"what it does in one sentence\",\n"
           "  \"functions\": [{\"name\": \"my_plugin_run\", \"description\": \"...\", "
           "\"parameters\": {\"type\": \"object\"}}],\n"
           "  \"buttons\": [{\"id\": \"my-plugin-btn\", \"label\": \"Run\", "
           "\"icon\": \"oc-puzzle-2\", \"action\": \"run-my-plugin\"}],\n"
           "  \"agents\": [{\"name\": \"My Helper\", \"prompt\": \"role prompt\", \"model\": \"\"}],\n"
           "  \"ui\": {\"css\": \"\", \"theme\": \"auto\"}\n"
           "}\n"
           "```\n"
           "\n"
           "## Rules\n"
           "\n"
           "- `id`: slug `[a-z0-9-]` 2..48 chars. Function names use `_`, never `-`.\n"
           "- Keep the whole file <= 64KB; max 32 functions/buttons, 16 agents.\n"
           "- `enabled: false` hides the plugin without deleting it.\n"
           "- Buttons may only add actions; never remove core chat controls.\n"
           "- Functions must have `name` + `description`; agents need `name` + `prompt`.\n"
           "- Validate with `zeno_plugin_validate` before saving.\n"
           "- Rename with `zeno_plugin_rename` (display name only, id is stable).\n"
           "- Ask-then-build: confirm name + one-sentence description, then emit JSON.\n";
}

/* Agent tool: generate the JSON, save automatically, make it available. */
static int studio_arg_string(const char *args_json, const char *key, char **out) {
    char *error = NULL;
    ZjNode *root;
    const char *value;
    *out = NULL;
    if (args_json == NULL || key == NULL) return 0;
    root = zj_parse(args_json, &error);
    free(error);
    if (root == NULL) return 0;
    value = zj_string(zj_object_get(root, key));
    if (value != NULL) *out = zeno_strdup(value);
    zj_free(root);
    return *out != NULL;
}

static int tool_create_plugin(void *context, const char *args_json, char **output, char **error) {
    char *request = NULL;
    char *hint = NULL;
    char *dir = NULL;
    char *plugin = NULL;
    int ok = 0;
    const char *plugins_dir;
    char err[256];
    (void)context;
    if (output != NULL) *output = NULL;
    studio_arg_string(args_json, "request", &request);
    studio_arg_string(args_json, "name_hint", &hint);
    studio_arg_string(args_json, "name", &dir);
    if (dir == NULL) studio_arg_string(args_json, "plugins_dir", &dir);
    if (request == NULL || *request == '\0') {
        if (error != NULL) *error = zeno_strdup("field 'request' is required");
        free(request);
        free(hint);
        free(dir);
        return 0;
    }
    if (dir != NULL && *dir != '\0') plugins_dir = dir;
    else if (getenv("ZENO_PLUGINS_DIR") != NULL && *getenv("ZENO_PLUGINS_DIR") != '\0')
        plugins_dir = getenv("ZENO_PLUGINS_DIR");
    else
        plugins_dir = "zeno_plugins";
    plugin = zeno_plugin_create(request, hint);
    if (plugin == NULL) {
        if (error != NULL) *error = zeno_strdup("could not generate plugin JSON");
        free(request);
        free(hint);
        free(dir);
        return 0;
    }
    if (!zeno_plugin_validate(plugin, err, sizeof(err))) {
        if (error != NULL) *error = zeno_format("generated invalid plugin: %s", err);
        free(plugin);
        free(request);
        free(hint);
        free(dir);
        return 0;
    }
    if (!zeno_plugin_save(plugins_dir, plugin)) {
        if (error != NULL) *error = zeno_strdup("could not save plugin file");
        free(plugin);
        free(request);
        free(hint);
        free(dir);
        return 0;
    }
    if (output != NULL) *output = plugin;
    else free(plugin);
    ok = 1;
    free(request);
    free(hint);
    free(dir);
    return ok;
}

int zeno_register_studio_tools(ZenoRegistry *registry, const char *plugins_dir) {
    ZenoToolDefinition definition;
    (void)plugins_dir;
    if (registry == NULL) return 0;
    memset(&definition, 0, sizeof(definition));
    definition.name = "create_plugin";
    definition.description = "Generate a Zeno JSON plugin from a user request, save it automatically, and make it available. Use after confirming plugin name + one-sentence description.";
    definition.parameters_json = "{\"type\":\"object\",\"properties\":{\"request\":{\"type\":\"string\",\"description\":\"what the plugin must do\"},\"name_hint\":{\"type\":\"string\",\"description\":\"short display name hint\"},\"plugins_dir\":{\"type\":\"string\",\"description\":\"plugins directory (default zeno_plugins)\"}},\"required\":[\"request\"]}";
    definition.effect = ZENO_EFFECT_WRITE_LOCAL;
    definition.requires_approval = 0;
    definition.timeout_ms = 120000;
    definition.max_retries = 0;
    return zeno_registry_register(registry, definition, tool_create_plugin, NULL);
}
