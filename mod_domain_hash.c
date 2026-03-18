/*
 * mod_domain_hash.c
 * Apache module to map domains to SHA1-based directory paths
 * 
 * Compilation:
 * apxs -i -a -c mod_domain_hash.c -lcrypto
 * 
 * Configuration in httpd.conf:
 * LoadModule domain_hash_module modules/mod_domain_hash.so
 * DomainHashEnable On
 * DomainHashBase /var/sites/sites1
 * DomainHashBase /var/sites/sites2
 * RedirectMap /path/to/file/rules-map.txt
 * RedirectMapRoot /var/sites/redirector
 */

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"
#include "apr_strings.h"
#include <openssl/sha.h>
#include <sys/stat.h>
#include <ctype.h>
#include <regex.h>

module AP_MODULE_DECLARE_DATA domain_hash_module;

/* Forward declarations */
static apr_status_t load_redirect_patterns(apr_pool_t *pool, const char *filepath, 
                                           apr_array_header_t **patterns, server_rec *s);

#define MAX_BASE_PATHS 16

typedef struct {
    int enabled;
    apr_array_header_t *base_paths;  /* Array of const char* base paths */
    const char *redirect_map_file;   /* Path to redirect rules file */
    const char *redirect_map_root;   /* Document root for redirected requests */
    apr_array_header_t *redirect_patterns; /* Compiled regex patterns */
} domain_hash_config;

/* Create per-server configuration */
static void *create_domain_hash_server_config(apr_pool_t *pool, server_rec *s)
{
    domain_hash_config *cfg = apr_pcalloc(pool, sizeof(domain_hash_config));
    cfg->enabled = 0;
    cfg->base_paths = apr_array_make(pool, 4, sizeof(const char *));
    cfg->redirect_map_file = NULL;
    cfg->redirect_map_root = NULL;
    cfg->redirect_patterns = NULL;
    return cfg;
}

/* Merge server configurations */
static void *merge_domain_hash_server_config(apr_pool_t *pool, void *base_conf, void *new_conf)
{
    domain_hash_config *base = (domain_hash_config *)base_conf;
    domain_hash_config *new = (domain_hash_config *)new_conf;
    domain_hash_config *cfg = apr_pcalloc(pool, sizeof(domain_hash_config));
    
    cfg->enabled = new->enabled ? new->enabled : base->enabled;
    
    /* Merge base_paths arrays: use new's paths if any, otherwise use base's */
    if (new->base_paths && new->base_paths->nelts > 0) {
        cfg->base_paths = apr_array_copy(pool, new->base_paths);
    } else if (base->base_paths && base->base_paths->nelts > 0) {
        cfg->base_paths = apr_array_copy(pool, base->base_paths);
    } else {
        cfg->base_paths = apr_array_make(pool, 4, sizeof(const char *));
    }
    
    /* Merge redirect map settings */
    cfg->redirect_map_file = new->redirect_map_file ? new->redirect_map_file : base->redirect_map_file;
    cfg->redirect_map_root = new->redirect_map_root ? new->redirect_map_root : base->redirect_map_root;
    cfg->redirect_patterns = new->redirect_patterns ? new->redirect_patterns : base->redirect_patterns;
    
    return cfg;
}

/* Configuration directive handlers */
static const char *set_domain_hash_enable(cmd_parms *cmd, void *dummy, int flag)
{
    domain_hash_config *cfg = ap_get_module_config(cmd->server->module_config, &domain_hash_module);
    cfg->enabled = flag;
    return NULL;
}

static const char *set_domain_hash_base(cmd_parms *cmd, void *dummy, const char *arg)
{
    domain_hash_config *cfg = ap_get_module_config(cmd->server->module_config, &domain_hash_module);
    const char **new_path;
    
    /* Check if we've exceeded maximum base paths */
    if (cfg->base_paths->nelts >= MAX_BASE_PATHS) {
        return apr_psprintf(cmd->pool, "DomainHashBase: Maximum of %d base paths exceeded", MAX_BASE_PATHS);
    }
    
    /* Add the new base path to the array */
    new_path = apr_array_push(cfg->base_paths);
    *new_path = arg;
    
    return NULL;
}

static const char *set_redirect_map(cmd_parms *cmd, void *dummy, const char *arg)
{
    domain_hash_config *cfg = ap_get_module_config(cmd->server->module_config, &domain_hash_module);
    cfg->redirect_map_file = arg;
    return NULL;
}

static const char *set_redirect_map_root(cmd_parms *cmd, void *dummy, const char *arg)
{
    domain_hash_config *cfg = ap_get_module_config(cmd->server->module_config, &domain_hash_module);
    cfg->redirect_map_root = arg;
    return NULL;
}

/* Load and compile redirect patterns from file */
static apr_status_t load_redirect_patterns(apr_pool_t *pool, const char *filepath, 
                                           apr_array_header_t **patterns, server_rec *s)
{
    apr_file_t *file;
    apr_status_t rv;
    char line[1024];
    regex_t *compiled_regex;
    int regex_result;
    char error_buffer[256];
    
    if (!filepath || !patterns) {
        return APR_EINVAL;
    }
    
    /* Open the redirect map file */
    rv = apr_file_open(&file, filepath, APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool);
    if (rv != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, rv, s,
                     "mod_domain_hash: Failed to open redirect map file: %s", filepath);
        return rv;
    }
    
    /* Initialize patterns array */
    *patterns = apr_array_make(pool, 10, sizeof(regex_t *));
    
    /* Read file line by line */
    while (apr_file_gets(line, sizeof(line), file) == APR_SUCCESS) {
        char *trimmed = line;
        size_t len;
        
        /* Trim leading whitespace */
        while (*trimmed && isspace((unsigned char)*trimmed)) {
            trimmed++;
        }
        
        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }
        
        /* Trim trailing whitespace and newline */
        len = strlen(trimmed);
        while (len > 0 && isspace((unsigned char)trimmed[len - 1])) {
            trimmed[--len] = '\0';
        }
        
        /* Skip if line became empty after trimming */
        if (len == 0) {
            continue;
        }
        
        /* Allocate and compile regex */
        compiled_regex = apr_palloc(pool, sizeof(regex_t));
        regex_result = regcomp(compiled_regex, trimmed, REG_EXTENDED | REG_NOSUB);
        
        if (regex_result != 0) {
            regerror(regex_result, compiled_regex, error_buffer, sizeof(error_buffer));
            ap_log_error(APLOG_MARK, APLOG_WARNING, 0, s,
                        "mod_domain_hash: Failed to compile regex pattern '%s': %s",
                        trimmed, error_buffer);
            continue;
        }
        
        /* Add compiled pattern to array */
        *(regex_t **)apr_array_push(*patterns) = compiled_regex;
        
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s,
                    "mod_domain_hash: Loaded redirect pattern: %s", trimmed);
    }
    
    apr_file_close(file);
    
    ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, "mod_domain_hash: Loaded %d redirect patterns from %s", (*patterns)->nelts, filepath);
    
    return APR_SUCCESS;
}

/* Check if URI matches any redirect pattern */
static int matches_redirect_pattern(const char *uri, apr_array_header_t *patterns)
{
    int i;
    regex_t **pattern_list;
    
    if (!uri || !patterns || patterns->nelts == 0) {
        return 0;
    }
    
    pattern_list = (regex_t **)patterns->elts;
    
    for (i = 0; i < patterns->nelts; i++) {
        int result = regexec(pattern_list[i], uri, 0, NULL, 0);
        if (result == 0) {
            return 1;
        }
    }
    
    return 0;
}

/* Check if string starts with prefix (safe version) */
static int starts_with(const char *text, const char *prefix)
{
    size_t text_len, prefix_len;
    
    if (!text || !prefix) {
        return 0;
    }
    
    text_len = strlen(text);
    prefix_len = strlen(prefix);
    
    /* Ensure text is at least as long as prefix */
    if (text_len < prefix_len) {
        return 0;
    }
    
    return strncmp(text, prefix, prefix_len) == 0;
}

/* Generate SHA1 hash and create path */
static char *generate_hash_path(apr_pool_t *pool, const char *base_path, const char *domain)
{
    unsigned char hash[SHA_DIGEST_LENGTH];
    char hex[SHA_DIGEST_LENGTH * 2 + 1];
    char *path;
    const char *clean_domain;
    char *clean_base_path;
    size_t base_len;
    int i;
    
    /* Remove trailing slash from base_path if present */
    base_len = strlen(base_path);
    if (base_len > 0 && base_path[base_len - 1] == '/') {
        clean_base_path = apr_pstrndup(pool, base_path, base_len - 1);
    } else {
        clean_base_path = (char *)base_path;
    }
    
    /* Strip "www." prefix if present */
    if (starts_with(domain, "www.")) {
        clean_domain = domain + 4;  /* Skip "www." */
    } else {
        clean_domain = domain;
    }
    
    /* Calculate SHA1 hash */
    SHA1((unsigned char *)clean_domain, strlen(clean_domain), hash);
    
    /* Convert first 7 bytes to hex */
    for (i = 0; i < 7; i++) {
        sprintf(hex + (i * 2), "%02x", hash[i]);
    }
    hex[14] = '\0';
    
    /* Build path: /var/www/7/7/6/0/5/7/1/domain.com */
    path = apr_psprintf(pool, "%s/%c/%c/%c/%c/%c/%c/%c/%s",
                        clean_base_path,
                        hex[0], hex[1], hex[2], hex[3],
                        hex[4], hex[5], hex[6],
                        clean_domain);
    
    return path;
}

/* Check if directory exists */
static int directory_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 1;
    }
    return 0;
}

/* Sanitize URI to prevent directory traversal attacks */
static int is_safe_uri(const char *uri)
{
    const char *p;
    
    if (!uri || *uri == '\0') {
        return 0;
    }
    
    /* URI must start with / */
    if (uri[0] != '/') {
        return 0;
    }
    
    /* Check for directory traversal patterns */
    p = uri;
    while (*p) {
        /* Check for ../ or ..\ patterns */
        if (p[0] == '.' && p[1] == '.') {
            /* Check if followed by path separator or end of string */
            if (p[2] == '/' || p[2] == '\\' || p[2] == '\0') {
                return 0;
            }
            /* Check if preceded by path separator or start of string */
            if (p == uri || p[-1] == '/' || p[-1] == '\\') {
                return 0;
            }
        }
        
        /* Reject backslashes (Windows-style paths) */
        if (*p == '\\') {
            return 0;
        }
        
        /* Reject null bytes */
        if (*p == '\0') {
            break;
        }
        
        p++;
    }
    
    return 1;
}

/* Verify that resolved path is within document root */
static int is_path_within_root(const char *resolved_path, const char *doc_root)
{
    size_t root_len;
    
    if (!resolved_path || !doc_root) {
        return 0;
    }
    
    root_len = strlen(doc_root);
    
    /* Resolved path must start with document root */
    if (strncmp(resolved_path, doc_root, root_len) != 0) {
        return 0;
    }
    
    /* If doc_root doesn't end with /, ensure next char is / or end of string */
    if (doc_root[root_len - 1] != '/') {
        if (resolved_path[root_len] != '/' && resolved_path[root_len] != '\0') {
            return 0;
        }
    }
    
    return 1;
}

/* Translate name hook - this runs early and can modify the filename */
static int domain_hash_translate_name(request_rec *r)
{
    domain_hash_config *cfg;
    const char *host;
    char *vhost_path;
    char *host_lower;
    char *port;
    int i;
    core_server_config *core_cfg;
    
    cfg = ap_get_module_config(r->server->module_config, &domain_hash_module);
    
    /* Skip if module is not enabled */
    if (!cfg->enabled) {
        return DECLINED;
    }
    
    /* For subrequests and internal redirects, preserve the document root from main request */
    if (r->main || r->prev) {
        request_rec *parent = r->main ? r->main : r->prev;
        const char *redirect_matched = apr_table_get(parent->notes, "REDIRECT_MATCHED");
        
        if (!redirect_matched) {
            redirect_matched = apr_table_get(parent->subprocess_env, "REDIRECT_MATCHED");
        }
        
        if (redirect_matched && strcmp(redirect_matched, "1") == 0) {
            /* Copy the document root from parent request */
            const char *parent_docroot = ap_document_root(parent);
            if (parent_docroot) {
                ap_set_document_root(r, parent_docroot);
                ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                             "mod_domain_hash: Subrequest/internal redirect - preserving document root '%s'",
                             parent_docroot);
            }
            return DECLINED;
        }
    }
    
    /* Get the Host header */
    host = apr_table_get(r->headers_in, "Host");
    
    /* If no host header, decline */
    if (!host || *host == '\0') {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_domain_hash: No Host header, declining");
        return DECLINED;
    }
    
    /* Duplicate and process host */
    host_lower = apr_pstrdup(r->pool, host);
    
    /* Remove port number if present */
    port = strchr(host_lower, ':');
    if (port) {
        *port = '\0';
    }

    /* Convert to lowercase */
    for (i = 0; host_lower[i]; i++) {
        host_lower[i] = tolower((unsigned char)host_lower[i]);
    }
    
    /* Check if any base paths are configured */
    if (!cfg->base_paths || cfg->base_paths->nelts == 0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_domain_hash: No base paths configured, declining");
        return DECLINED;
    }
    
    /* Try each base path in order until we find one that exists */
    const char **paths = (const char **)cfg->base_paths->elts;
    vhost_path = NULL;

    for (i = 0; i < cfg->base_paths->nelts; i++) {
        char *candidate_path = generate_hash_path(r->pool, paths[i], host_lower);
        
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_domain_hash: Host='%s' trying path='%s'",
                      host_lower, candidate_path);
        
        if (directory_exists(candidate_path)) {
            vhost_path = candidate_path;
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                          "mod_domain_hash: Found matching directory at '%s'",
                          vhost_path);
            break;
        }
    }
    
    /* If no matching directory found in any base path, decline */
    if (!vhost_path) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_domain_hash: No matching directory found for host '%s' in any base path, declining",
                      host_lower);
        return DECLINED;
    }
    
    /* Check if /public subdirectory exists and use it if available */
    char *public_path = apr_pstrcat(r->pool, vhost_path, "/public", NULL);
    if (directory_exists(public_path)) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_domain_hash: Using public subdirectory: %s",
                      public_path);
        vhost_path = public_path;
    }
    
    /* Validate URI to prevent directory traversal attacks */
    if (!is_safe_uri(r->uri)) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                      "mod_domain_hash: Rejected unsafe URI: %s",
                      r->uri);
        return HTTP_FORBIDDEN;
    }
    
    /* Build the filename from document root + URI */
    if (r->uri && r->uri[0] == '/') {
        r->filename = apr_pstrcat(r->pool, vhost_path, r->uri, NULL);
    } else {
        r->filename = apr_pstrcat(r->pool, vhost_path, "/", r->uri, NULL);
    }

    /* Verify the resolved path is within the document root */
    if (!is_path_within_root(r->filename, vhost_path)) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                      "mod_domain_hash: Path traversal attempt detected. "
                      "Filename '%s' is outside document root '%s'",
                      r->filename, vhost_path);
        return HTTP_FORBIDDEN;
    }

    /* Check redirect map first if configured */
    if (cfg->redirect_map_file && cfg->redirect_map_root && r->uri && cfg->redirect_patterns) {
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                     "mod_domain_hash: Checking redirect for URI '%s'", r->uri);
        
        /* Check if URI matches any redirect pattern */
        if (matches_redirect_pattern(r->uri, cfg->redirect_patterns)) {
            char *redirect_root = (char *)cfg->redirect_map_root;
            size_t root_len = strlen(redirect_root);
            
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                         "mod_domain_hash: URI '%s' MATCHED redirect pattern", r->uri);
            
            /* Remove trailing slash from redirect root if present */
            if (root_len > 0 && redirect_root[root_len - 1] == '/') {
                redirect_root = apr_pstrndup(r->pool, redirect_root, root_len - 1);
            }
            
            /* Validate URI for security */
            if (!is_safe_uri(r->uri)) {
                ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                             "mod_domain_hash: Rejected unsafe URI in redirect: %s", r->uri);
                return HTTP_FORBIDDEN;
            }
            
            /* Set document root to redirect map root */
            ap_set_document_root(r, redirect_root);
            
            /* Build filename */
            r->filename = apr_pstrcat(r->pool, redirect_root, r->uri, NULL);
            
            /* Verify path is within redirect root */
            if (!is_path_within_root(r->filename, redirect_root)) {
                ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                             "mod_domain_hash: Path traversal attempt in redirect. "
                             "Filename '%s' is outside redirect root '%s'",
                             r->filename, redirect_root);
                return HTTP_FORBIDDEN;
            }
            
            r->canonical_filename = r->filename;
            
            /* Set environment variable to mark this as a redirect match */
            apr_table_set(r->subprocess_env, "REDIRECT_MATCHED", "1");
            apr_table_set(r->notes, "REDIRECT_MATCHED", "1");
            
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, 
                         "mod_domain_hash: URI '%s' matched redirect pattern, "
                         "using document root '%s', filename '%s'", 
                         r->uri, redirect_root, r->filename);
            
            /* Return OK to complete translation and prevent further processing */
            return OK;
        }
    }

    /* Set the document root for this request */
    ap_set_document_root(r, vhost_path);
    
    /* Set environment variable */
    apr_table_set(r->subprocess_env, "VHOST_PATH", vhost_path);

    /* Set canonical filename for proper .htaccess processing */
    r->canonical_filename = r->filename;

    ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "mod_domain_hash: Set document root to '%s', filename to '%s'", vhost_path, r->filename);
    
    /* Return OK - we've completed the translation */
    return OK;
}

/* Configuration directives */
static const command_rec domain_hash_directives[] = {
    AP_INIT_FLAG("DomainHashEnable", set_domain_hash_enable, NULL, RSRC_CONF,
                 "Enable or disable domain hash mapping (On|Off)"),
    AP_INIT_TAKE1("DomainHashBase", set_domain_hash_base, NULL, RSRC_CONF,
                  "Base path for domain hash directories (default: /var/www)"),
    AP_INIT_TAKE1("RedirectMap", set_redirect_map, NULL, RSRC_CONF,
                  "Path to redirect rules map file containing regex patterns"),
    AP_INIT_TAKE1("RedirectMapRoot", set_redirect_map_root, NULL, RSRC_CONF,
                  "Document root to use when redirect pattern matches"),
    {NULL}
};

/* Post-config hook to load redirect patterns after configuration is complete */
static int domain_hash_post_config(apr_pool_t *pconf, apr_pool_t *plog, 
                                   apr_pool_t *ptemp, server_rec *s)
{
    server_rec *server;
    
    /* Iterate through all server configurations */
    for (server = s; server; server = server->next) {
        domain_hash_config *cfg = ap_get_module_config(server->module_config, &domain_hash_module);
        
        /* Load redirect patterns if both file and root are configured */
        if (cfg && cfg->redirect_map_file && cfg->redirect_map_root && !cfg->redirect_patterns) {
            apr_status_t rv = load_redirect_patterns(pconf, cfg->redirect_map_file,
                                                     &cfg->redirect_patterns, server);
            if (rv != APR_SUCCESS) {
                ap_log_error(APLOG_MARK, APLOG_ERR, rv, server,
                            "mod_domain_hash: Failed to load redirect patterns from %s",
                            cfg->redirect_map_file);
                return HTTP_INTERNAL_SERVER_ERROR;
            }
        }
    }
    
    return OK;
}

/* Register hooks */
static void register_hooks(apr_pool_t *pool)
{
    /* post_config runs after configuration is complete */
    ap_hook_post_config(domain_hash_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    
    /* translate_name runs very early in request processing, before default handler
     * This is where we need to change the document root */
    ap_hook_translate_name(domain_hash_translate_name, NULL, NULL, APR_HOOK_FIRST);
}

/* Module declaration */
module AP_MODULE_DECLARE_DATA domain_hash_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                               /* create per-dir config */
    NULL,                               /* merge per-dir config */
    create_domain_hash_server_config,   /* create per-server config */
    merge_domain_hash_server_config,    /* merge per-server config */
    domain_hash_directives,             /* command table */
    register_hooks                      /* register hooks */
};