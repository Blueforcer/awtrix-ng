#pragma once

// The deploy lock of tc002_install.py: its token (TC002_DEPLOY_TOKEN) goes stale once it has not
// changed for the stale limit, and no deploy holds the lock longer than the hold limit.
#define AWTRIX_DEPLOY_TOKEN_STALE_SECONDS 90
#define AWTRIX_DEPLOY_HOLD_SECONDS 1800
#define AWTRIX_DEPLOY_TOKEN_TEXT_LIMIT 64

struct deploy_token_watch {
  int seen;
  long long seconds;
  long nanoseconds;
  double changed;
};

static inline int deploy_token_uptime(const char* text, double* uptime) {
  static const char prefix[] = "deploy ";
  for (const char* expected = prefix; *expected; ++expected, ++text)
    if (*text != *expected) return 0;
  double value = 0, unit = 1;
  int digits = 0, fraction = 0;
  for (;; ++text) {
    if (*text >= '0' && *text <= '9') {
      if (++digits > 15) return 0;
      if (fraction)
        value += (unit /= 10) * (*text - '0');
      else
        value = value * 10 + (*text - '0');
    } else if (*text == '.' && digits && !fraction) {
      fraction = 1;
    } else {
      break;
    }
  }
  if (!digits || (*text && !(*text == '\n' && !text[1]))) return 0;
  *uptime = value;
  return 1;
}

static inline double deploy_token_age(const char* text, double uptime, double wall_age) {
  double written;
  return deploy_token_uptime(text, &written) ? uptime - written : wall_age;
}

static inline int deploy_token_fresh(struct deploy_token_watch* watch, long long seconds,
                                     long nanoseconds, double now, double age, double stale) {
  if (!watch->seen || seconds != watch->seconds || nanoseconds != watch->nanoseconds) {
    watch->changed = watch->seen ? now : now - (age > 0 ? age : 0);
    watch->seen = 1;
    watch->seconds = seconds;
    watch->nanoseconds = nanoseconds;
  }
  return now - watch->changed < stale;
}
