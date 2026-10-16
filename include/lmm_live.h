#ifndef LMM_LIVE_H
#define LMM_LIVE_H

#include "lmm_map.h"
#include "lmm_rw.h"

struct lmm_live;

/* Convenience opener provided by the example lmmapper adapter. */
int lmm_live_open(struct lmm_live **out, struct lmm_map_backend *backend);
/* Takes ownership of *rw on a valid call, including failure; *rw is reset. The caller must provide a scratch region that may temporarily become executable. */
int lmm_live_open_with_rw(struct lmm_rw *rw, struct lmm_live **out, struct lmm_map_backend *backend);
int lmm_live_release(struct lmm_live *live);
int lmm_live_close(struct lmm_live *live);

#endif
