/*************************************************************************
    > File Name: as_addsub.c
    > Author: Kevin
    > Created Time: 2020-01-03
    > Description:
 ************************************************************************/

/* Includes *********************************************************** */
#include "projconfig.h"
#include "dev_config.h"
#include "utils.h"
#include "logging.h"

/* Defines  *********************************************************** */
#define ADD_SUB_MSG \
  "Node[%d]:  --- Sub [Element-Model(%d-%04x:%04x) <- 0x%04x]\n"
#define ADD_SUB_SUC_MSG \
  "Node[%d]:  --- Sub [Element-Model(%d-%04x:%04x) <- 0x%04x] SUCCESS\n"
#define ADD_SUB_FAIL_MSG \
  "Node[%d]:  --- Sub [Element-Model(%d-%04x:%04x) <- 0x%04x] FAILED, Err <0x%04x>\n"

#define ELEMENT_ITERATOR_INDEX  0
#define MODEL_ITERATOR_INDEX  1
#define SUB_ADDR_ITERATOR_INDEX  2

/* Global Variables *************************************************** */

/* Static Variables *************************************************** */

/* Static Functions Declaractions ************************************* */
#define ONCE_P(cache)                                                                \
  do {                                                                               \
    LOGD(                                                                            \
      ADD_SUB_MSG,                                                                   \
      cache->node->addr,                                                             \
      cache->iterators[ELEMENT_ITERATOR_INDEX],                                      \
      cache->vnm.vd,                                                                 \
      cache->vnm.md,                                                                 \
      cache->node->config.sublist->data[cache->iterators[SUB_ADDR_ITERATOR_INDEX]]); \
  } while (0)

#define SUC_P(cache)                                                                 \
  do {                                                                               \
    LOGD(                                                                            \
      ADD_SUB_SUC_MSG,                                                               \
      cache->node->addr,                                                             \
      cache->iterators[ELEMENT_ITERATOR_INDEX],                                      \
      cache->vnm.vd,                                                                 \
      cache->vnm.md,                                                                 \
      cache->node->config.sublist->data[cache->iterators[SUB_ADDR_ITERATOR_INDEX]]); \
  } while (0)

#define FAIL_P(cache, err)                                                          \
  do {                                                                              \
    LOGE(                                                                           \
      ADD_SUB_FAIL_MSG,                                                             \
      cache->node->addr,                                                            \
      cache->iterators[ELEMENT_ITERATOR_INDEX],                                     \
      cache->vnm.vd,                                                                \
      cache->vnm.md,                                                                \
      cache->node->config.sublist->data[cache->iterators[SUB_ADDR_ITERATOR_INDEX]], \
      err);                                                                         \
  } while (0)

/* Global Variables *************************************************** */
extern const char *stateNames[];

static const uint32_t events[] = {
  gecko_evt_mesh_config_client_model_sub_status_id
};

#define RELATE_EVENTS_NUM() (sizeof(events) / sizeof(uint32_t))
/* Static Variables *************************************************** */
typedef int (*funcPack)(void *, void *, void *);

/* Static Functions Declaractions ************************************* */
static int iter_addsub(config_cache_t *cache);

static int __addsub(config_cache_t *cache, mng_t *mng)
{
  struct gecko_msg_mesh_config_client_add_model_sub_rsp_t *arsp;
  struct gecko_msg_mesh_config_client_set_model_sub_rsp_t *srsp;
  uint16_t *retval = NULL;
  uint32_t *handle = NULL;

  if (cache->iterators[SUB_ADDR_ITERATOR_INDEX] == 0) {
    cache->vnm.vd =
      cache->iterators[MODEL_ITERATOR_INDEX] >= cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sigm_cnt
      ? cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].vm[cache->iterators[MODEL_ITERATOR_INDEX] - cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sigm_cnt].vid
      : 0xFFFF;
    cache->vnm.md =
      cache->iterators[MODEL_ITERATOR_INDEX] >= cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sigm_cnt
      ? cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].vm[cache->iterators[MODEL_ITERATOR_INDEX] - cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sigm_cnt].mid
      : cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sig_models[cache->iterators[MODEL_ITERATOR_INDEX]];

    srsp = gecko_cmd_mesh_config_client_set_model_sub(
      mng->cfg->subnets[0].netkey.id,
      cache->node->addr,
      cache->iterators[ELEMENT_ITERATOR_INDEX],
      cache->vnm.vd,
      cache->vnm.md,
      cache->node->config.sublist->data[cache->iterators[SUB_ADDR_ITERATOR_INDEX]]);
    retval = &srsp->result;
    handle = &srsp->handle;
  } else {
    cache->vnm.vd =
      cache->iterators[MODEL_ITERATOR_INDEX] >= cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sigm_cnt
      ? cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].vm[cache->iterators[MODEL_ITERATOR_INDEX] - cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sigm_cnt].vid
      : 0xFFFF;
    cache->vnm.md =
      cache->iterators[MODEL_ITERATOR_INDEX] >= cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sigm_cnt
      ? cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].vm[cache->iterators[MODEL_ITERATOR_INDEX] - cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sigm_cnt].mid
      : cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sig_models[cache->iterators[MODEL_ITERATOR_INDEX]];
    arsp = gecko_cmd_mesh_config_client_add_model_sub(
      mng->cfg->subnets[0].netkey.id,
      cache->node->addr,
      cache->iterators[ELEMENT_ITERATOR_INDEX],
      cache->vnm.vd,
      cache->vnm.md,
      cache->node->config.sublist->data[cache->iterators[SUB_ADDR_ITERATOR_INDEX]]);
    retval = &arsp->result;
    handle = &arsp->handle;
  }

  if (*retval != bg_err_success) {
    if (*retval == bg_err_out_of_memory) {
      OOM_SET(cache);
      return asr_oom;
    }
    FAIL_P(cache, *retval);
    err_set_to_end(cache, *retval, bgapi_em);
    LOGD("Node[%d]: To <<st_end>> State\n", cache->node->addr);
    return asr_bgapi;
  } else {
    ONCE_P(cache);
    WAIT_RESPONSE_SET(cache);
    cache->cc_handle = *handle;
    /* TODO: startTimer(cache, 1); */
  }

  return asr_suc;
}

bool addsub_guard(const config_cache_t *cache)
{
  return (cache->node->config.sublist && cache->node->config.sublist->len);
}

int addsub_entry(config_cache_t *cache, func_guard guard)
{
  if (guard && !guard(cache)) {
    LOGM("To Next State Since %s Guard Not Passed\n",
         stateNames[cache->state]);
    return asr_tonext;
  }

  return __addsub(cache, get_mng());
}

int addsub_inprg(const struct gecko_cmd_packet *evt, config_cache_t *cache)
{
  uint32_t evtid;
  ASSERT(cache);
  ASSERT(evt);

  evtid = BGLIB_MSG_ID(evt->header);
  /* TODO: startTimer(cache, 0); */
  switch (evtid) {
    case gecko_evt_mesh_config_client_model_sub_status_id:
    {
      WAIT_RESPONSE_CLEAR(cache);
      switch (evt->data.evt_mesh_config_client_model_sub_status.result) {
        case bg_err_success:
        case bg_err_mesh_no_friend_offer:
          /* TODO - This is a bug for btmesh stack */
          if (evt->data.evt_mesh_config_client_model_sub_status.result
              == bg_err_mesh_no_friend_offer) {
            LOGD("0x%04x Model doesn't support subscription\n", cache->vnm.md);
          }
          RETRY_CLEAR(cache);
          SUC_P(cache);
          break;
        case bg_err_timeout:
          /* bind any remaining_retry case here */
          if (!EVER_RETRIED(cache)) {
            cache->remaining_retry = ADD_SUB_RETRY_TIMES;
            EVER_RETRIED_SET(cache);
          } else if (cache->remaining_retry <= 0) {
            RETRY_CLEAR(cache);
            RETRY_OUT_PRINT(cache);
            err_set_to_end(cache, bg_err_timeout, bgevent_em);
            LOGD("Node[%d]: To <<st_end>> State\n", cache->node->addr);
          }
          return asr_suc;
          break;
        case bg_err_mesh_foundation_insufficient_resources:
          LOGW("Cannot sub more address, pass to next model\n");
          cache->iterators[SUB_ADDR_ITERATOR_INDEX] = cache->node->config.sublist->len - 1;
          break;
        default:
          FAIL_P(cache,
                 evt->data.evt_mesh_config_client_model_sub_status.result);
          err_set_to_end(cache, bg_err_timeout, bgevent_em);
          LOGW("To <<End>> State\n");
          return asr_suc;
      }

      if (iter_addsub(cache) == 1) {
        cache->next_state = -1;
        return asr_suc;
      }

      return __addsub(cache, get_mng());
    }
    break;

    default:
      LOGE("Unexpected event [0x%08x] happend in %s state.\n",
           evtid,
           stateNames[cache->state]);
      return asr_unspec;
  }
  return asr_suc;
}

int addsub_retry(config_cache_t *cache, int reason)
{
  int ret;
  ASSERT(cache);
  ASSERT(reason < retry_on_max_em);

  ret = __addsub(cache, get_mng());

  if (ret == asr_suc) {
    switch (reason) {
      case on_timeout_em:
        if (!EVER_RETRIED(cache) || cache->remaining_retry-- <= 0) {
          ASSERT(0);
        }
        RETRY_ONCE_PRINT(cache);
        break;
      case on_oom_em:
        ASSERT(OOM(cache));
        OOM_ONCE_PRINT(cache);
        OOM_CLEAR(cache);
        break;
      case on_guard_timer_expired_em:
        ASSERT(cache->expired);
        EXPIRED_ONCE_PRINT(cache);
        cache->expired = 0;
        break;
    }
  }
  return ret;
}

int addsub_exit(void *p)
{
  return asr_suc;
}

bool is_addsub_pkts(uint32_t evtid)
{
  int i;
  for (i = 0; i < RELATE_EVENTS_NUM(); i++) {
    if (BGLIB_MSG_ID(evtid) == events[i]) {
      return 1;
    }
  }
  return 0;
}

static int iter_addsub(config_cache_t *cache)
{
  if (++cache->iterators[SUB_ADDR_ITERATOR_INDEX] == cache->node->config.sublist->len) {
    cache->iterators[SUB_ADDR_ITERATOR_INDEX] = 0;
    if (++cache->iterators[MODEL_ITERATOR_INDEX]
        == cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].sigm_cnt
        + cache->dcd.elems[cache->iterators[ELEMENT_ITERATOR_INDEX]].vm_cnt) {
      cache->iterators[MODEL_ITERATOR_INDEX] = 0;
      if (++cache->iterators[ELEMENT_ITERATOR_INDEX] == cache->dcd.element_cnt) {
        cache->iterators[ELEMENT_ITERATOR_INDEX] = 0;
        return 1;
      }
    }
  }
  return 0;
}
