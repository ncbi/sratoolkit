#include "ascp-priv.h" /* ascp_path */
#include <klib/printf.h> /* string_printf */
#include <assert.h>
#include <limits.h> /* PATH_MAX */
#include <stdlib.h> /* getenv */

bool ascp_path(const char **cmd, const char **key) {
    static int idx = 0;
    static const char k[] = "/opt/aspera/etc/asperaweb_id_dsa.putty";
    static const char *c[]
        = { "ascp", "/usr/bin/ascp", "/opt/aspera/bin/ascp" };
    assert(cmd && key);
    if (idx < sizeof c / sizeof c[0]) {
        *cmd = c[idx];
        *key = k;
        ++idx;
        return true;
    }
    else if (idx == sizeof c / sizeof c[0]) {
        rc_t rc = 0;
        static char k[PATH_MAX] = "";
        static char c[PATH_MAX] = "";
        if (k[0] == '\0') {
            size_t num_writ = 0;
            const char* home = getenv("HOME");
            if (home == NULL) {
                home = "";
            }
            rc = string_printf(k, sizeof k, &num_writ,
                    "%s/.aspera/connect/etc/asperaweb_id_dsa.putty", home);
            if (rc != 0 || num_writ >= PATH_MAX) {
                assert(0);
                k[0] = '\0';
            }
            else {
                rc = string_printf(c, sizeof c, &num_writ,
                    "%s/.aspera/connect/bin/ascp", home);
                if (rc != 0 || num_writ >= PATH_MAX) {
                    assert(0);
                    c[0] = '\0';
                }
            }
        }
        if (rc != 0) {
            *cmd = *key = NULL;
            idx = 0;
            return false;
        }
        else {
            *cmd = c;
            *key = k;
            ++idx;
            return true;
        }
    }
    else {
        *cmd = *key = NULL;
        idx = 0;
        return false;
    }
}