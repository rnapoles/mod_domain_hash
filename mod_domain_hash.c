/*
 * mod_domain_hash.c
 * Apache module to map domains to SHA1-based directory paths
 * 
 * Compilation:
 * apxs -i -a -c mod_domain_hash.c -lcrypto
 * 
 * Configuration in httpd.conf:
 * LoadModule domain_hash_module modules/mod_domain_hash.so
 * DomainHashBase /var/www
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

typedef struct {
    int enabled;
    const char *base_path;
} domain_hash_config;

/* Create per-server configuration */
static void *create_domain_hash_server_config(apr_pool_t *pool, server_rec *s)
{
    domain_hash_config *cfg = apr_pcalloc(pool, sizeof(domain_hash_config));
    cfg->enabled = 0;
    cfg->base_path = "/var/www";
    return cfg;
}

/* Merge server configurations */
static void *merge_domain_hash_server_config(apr_pool_t *pool, void *base_conf, void *new_conf)
{
    domain_hash_config *base = (domain_hash_config *)base_conf;
    domain_hash_config *new = (domain_hash_config *)new_conf;
    domain_hash_config *cfg = apr_pcalloc(pool, sizeof(domain_hash_config));
    
    cfg->enabled = new->enabled ? new->enabled : base->enabled;
    cfg->base_path = new->base_path ? new->base_path : base->base_path;
    
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
    cfg->base_path = arg;
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
    int i;
    
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
                        base_path,
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
    
    /* Generate hash-based path */
    vhost_path = generate_hash_path(r->pool, cfg->base_path, host_lower);
    
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_domain_hash: Host='%s' mapped to path='%s'",
                  host_lower, vhost_path);
    
    /* Check if directory exists */
    if (!directory_exists(vhost_path)) {
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                      "mod_domain_hash: Directory does not exist: %s",
                      vhost_path);
        return HTTP_NOT_FOUND;
    }
    
    /* Check if /public subdirectory exists and use it if available */
    char *public_path = apr_pstrcat(r->pool, vhost_path, "/public", NULL);
    if (directory_exists(public_path)) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_domain_hash: Using public subdirectory: %s",
                      public_path);
        vhost_path = public_path;
    }
    
    /* Get core configuration and update document root */
    core_cfg = ap_get_core_module_config(r->per_dir_config);
    core_cfg->ap_document_root = vhost_path;
    
    /* Set the document root for this request */
    ap_set_document_root(r, vhost_path);
    
    /* Set environment variable */
    apr_table_set(r->subprocess_env, "VHOST_PATH", vhost_path);
    
    /* Update the filename to be relative to new document root */
    /* This is critical - we need to reconstruct the filename */
    if (r->uri && r->uri[0] == '/') {
        r->filename = apr_pstrcat(r->pool, vhost_path, r->uri, NULL);
    } else {
        r->filename = apr_pstrcat(r->pool, vhost_path, "/", r->uri, NULL);
    }
    
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_domain_hash: Set filename to '%s'", r->filename);
    
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