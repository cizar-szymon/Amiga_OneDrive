/* Settings: ENVARC:AmiDrive/AmiDrive.prefs, lines "KEY=value" ('#' starts a comment).
 * Order of precedence (main.c): Shell arguments > Tool Types > the prefs file. */
#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define CFG_PATH_SIZE 256

#define CFG_DIR        "ENVARC:AmiDrive"
#define CFG_PREFS_FILE "ENVARC:AmiDrive/AmiDrive.prefs"
#define CFG_TOKEN_FILE "ENVARC:AmiDrive/token"

#define CFG_INTERVAL_DEFAULT 3          /* minutes between checks in the background */
#define CFG_ACCOUNTS 3                  /* OneDrive accounts; one is active at a time */

/* Accounts: account 1 keeps LOCAL, REMOTE, STATE, NAME in the prefs file with the other
 * settings; accounts 2 and 3 keep them in ENVARC:AmiDrive/Account<n>.prefs. Each account
 * has its own token (token, token2, token3). */

typedef struct {
    char local[CFG_PATH_SIZE];          /* Amiga directory, e.g. Work:OneDrive */
    char remote[CFG_PATH_SIZE];         /* OneDrive folder, e.g. /Amiga */
    char state[CFG_PATH_SIZE];          /* "" = <local>/.AmiDrive */
    char charset[8];                    /* LATIN1 | LATIN2 */
    char upload[4];                     /* YES: changes on the Amiga are sent | NO */
    char clock[8];                      /* ASK: window when the clock is wrong | AUTO | NO */
    char appicon[4];                    /* YES: "OneDrive" icon on the Workbench while running | NO */
    long interval;                      /* minutes */
    char client_id[48];
    char tenant[40];
    int account;                        /* the active account, 1..CFG_ACCOUNTS */
    char name[64];                      /* the account as shown (owner of the OneDrive) */
} Config;

void config_defaults(Config *c);

/* Apply the keys present in text (unknown keys are ignored). Returns AD_OK, or
 * AD_ERR_PROTO when a value is invalid (that key is then left unchanged). */
int config_parse(Config *c, const char *text);

/* Set one key, e.g. from an argument or a Tool Type. Returns AD_OK, AD_ERR_PROTO
 * for an invalid value, AD_ERR for an unknown key. */
int config_set(Config *c, const char *key, const char *value);

/* Write all settings as text. Returns the length or AD_ERR_TOOBIG. */
int config_format(const Config *c, char *buf, size_t size);

/* Only the keys of an account (LOCAL, REMOTE, STATE, NAME), for Account<n>.prefs. */
int config_format_account(const Config *c, char *buf, size_t size);

/* Empty the keys of an account (before reading another account's file). */
void config_account_clear(Config *c);

/* Value of KEY in "KEY=value" text (case-insensitive key, trailing spaces cut).
 * Returns the length, AD_ERR (missing) or AD_ERR_TOOBIG. */
int kv_get(const char *text, const char *key, char *out, size_t size);

#endif
