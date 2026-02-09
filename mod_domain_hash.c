/*
 * mod_domain_hash.c
 * Apache module to map domains to SHA1-based directory paths
 * 
 * Compilation:
 * apxs -i -a -c mod_domain_hash.c -lcrypto
 * 
 * Configuration in httpd.conf:
 * LoadModule domain_hash_module modules/mod_domain_hash.so
 * DomainHashBase /var/sites/sites1
 * DomainHashBase /var/sites/sites2
 * DomainHashEnable On
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

module AP_MODULE_DECLARE_DATA domain_hash_module;

#define MAX_BASE_PATHS 16

typedef struct {
    int enabled;
    apr_array_header_t *base_paths;  /* Array of const char* base paths */
} domain_hash_config;

/* Create per-server configuration */
static void *create_domain_hash_server_config(apr_pool_t *pool, server_rec *s)
{
    domain_hash_config *cfg = apr_pcalloc(pool, sizeof(domain_hash_config));
    cfg->enabled = 0;
    cfg->base_paths = apr_array_make(pool, 4, sizeof(const char *));
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
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
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
    
    /* Set the document root for this request */
    ap_set_document_root(r, vhost_path);
    
    /* Set environment variable */
    apr_table_set(r->subprocess_env, "VHOST_PATH", vhost_path);
    
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
    
    /* Set canonical filename for proper .htaccess processing */
    r->canonical_filename = r->filename;
    
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_domain_hash: Set document root to '%s', filename to '%s'", 
                  vhost_path, r->filename);
    
    /* Return OK - we've completed the translation */
    return OK;
}

/* Configuration directives */
static const command_rec domain_hash_directives[] = {
    AP_INIT_FLAG("DomainHashEnable", set_domain_hash_enable, NULL, RSRC_CONF,
                 "Enable or disable domain hash mapping (On|Off)"),
    AP_INIT_TAKE1("DomainHashBase", set_domain_hash_base, NULL, RSRC_CONF,
                  "Base path for domain hash directories (default: /var/www)"),
    {NULL}
};

/* Register hooks */
static void register_hooks(apr_pool_t *pool)
{
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